//
// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iterator>
#include <memory>
#include <unordered_set>
#include <vector>

#include <android-base/logging.h>
#include <fruit/fruit.h>
#include <json/json.h>
#include <string.h>
#include <sys/socket.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/socket2socket_proxy.h"
#include "host/commands/run_cvd/launch/launch.h"
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

const std::string kControlSocketName = "control_sock";
class Ti50Emulator : public vm_manager::VmmDependencyCommand {
 public:
  INJECT(Ti50Emulator(const CuttlefishConfig::InstanceSpecific& instance,
                      LogTeeCreator& log_tee))
      : instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    if (!Enabled()) {
      LOG(ERROR) << "ti50 emulator is not enabled";
      return {};
    }

    Command command(instance_.ti50_emulator());
    command.AddParameter("-s");
    command.AddParameter("--control_socket=",
                         instance_.PerInstancePath(kControlSocketName));
    command.AddParameter("-p=", instance_.instance_dir());

    std::vector<MonitorCommand> commands;
    commands.emplace_back(
        CF_EXPECT(log_tee_.CreateFullLogTee(command, "ti50")));
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "Ti50Emulator"; }
  bool Enabled() const override { return !instance_.ti50_emulator().empty(); }

  // StatusCheckCommandSource
  Result<void> WaitForAvailability() const {
    if (!Enabled()) {
      return {};
    }

    // Wait for control socket sending "READY".
    SharedFD sock = SharedFD::Accept(*ctrl_sock_);
    const char kExpectedReadyStr[] = "READY";
    char buf[std::size(kExpectedReadyStr)];
    CF_EXPECT_NE(sock->Read(buf, sizeof(buf)), 0);
    CF_EXPECT(!strcmp(buf, "READY"), "Ti50 emulator should return 'READY'");

    CF_EXPECT(ResetGPIO());

    // Initialize TPM socket
    CF_EXPECT(InitializeTpm());

    return {};
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }

  Result<void> ResultSetup() override {
    // Socket proxy
    ctrl_sock_ = SharedFD::SocketLocalServer(
        instance_.PerInstancePath(kControlSocketName), false, SOCK_STREAM,
        0777);
    if (!ctrl_sock_->IsOpen()) {
      LOG(ERROR) << "Unable to create unix ctrl_sock server: "
                 << ctrl_sock_->StrError();
    }

    return {};
  }

  Result<void> ResetGPIO() const {
    // Write '1' to 'gpioPltRst' to initialize the emulator.
    std::string gpio_sock = instance_.PerInstancePath("gpioPltRst");
    CF_EXPECT(WaitForUnixSocket(gpio_sock, 30));

    // Wait for the emulator's internal state to be initialized.
    // Since the emulator polls the socket at 100 ms intervals before
    // initializing , 1 second sleep after the socket being ready should be a
    // sufficiently long.
    // https://crrev.com/7447dbd20aee11809e89e04bb2fcb2a1476febe1/tpm2-simulator/tpm_executor_ti50_impl.cc#171
    sleep(1);

    SharedFD cl = SharedFD::SocketLocalClient(gpio_sock, false, SOCK_STREAM);
    if (!cl->IsOpen()) {
      return CF_ERR("Failed to connect to gpioPltRst");
    }
    CF_EXPECT_EQ(cl->Write("1", 1), 1);

    LOG(INFO) << "ti50 emulator: reset GPIO!";
    return {};
  }

  Result<void> InitializeTpm() const {
    // Connects to direct_tpm_fifo socket, which is a bi-directional Unix domain
    // socket.
    std::string fifo_sock = instance_.PerInstancePath("direct_tpm_fifo");
    CF_EXPECT(WaitForUnixSocket(fifo_sock, 30));

    auto cl = SharedFD::SocketLocalClient(fifo_sock, false, SOCK_STREAM);
    if (!cl->IsOpen()) {
      return CF_ERR("Failed to connect to gpioPltRst");
    }

    const uint32_t kMaxRetryCount = 5;
    // TPM2_Startup command with SU_CLEAR
    const uint8_t kTpm2StartupCmd[] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x0c,
                                       0x00, 0x00, 0x01, 0x44, 0x00, 0x00};
    ssize_t cmd_size = sizeof(kTpm2StartupCmd);
    const uint8_t kExpectedResponse[] = {0x80, 0x01, 0x00, 0x00, 0x00,
                                         0x0a, 0x00, 0x00, 0x00, 0x00};
    ssize_t expected_response_size = sizeof(kExpectedResponse);
    for (int i = 0; i < kMaxRetryCount; i++) {
      CF_EXPECT_EQ(WriteAll(cl, (char*)kTpm2StartupCmd, cmd_size), cmd_size,
                   "failed to write TPM2_startup command");

      // Read a response.
      // First, read a 2-byte tag and 4-byte size.
      constexpr ssize_t kHeaderSize = 6;
      uint8_t resp_header[kHeaderSize] = {0};
      CF_EXPECT_EQ(ReadExact(cl, (char*)resp_header, kHeaderSize), kHeaderSize,
                   "failed to read TPM2_startup response header");
      uint8_t resp_size[4] = {resp_header[5], resp_header[4], resp_header[3],
                              resp_header[2]};
      uint32_t* response_size = reinterpret_cast<uint32_t*>(&resp_size);

      // Then, read the response body.
      uint32_t body_size = *response_size - kHeaderSize;
      std::vector<char> resp_body(body_size);
      CF_EXPECT_EQ(ReadExact(cl, &resp_body), body_size,
                   "failed to read TPM2_startup response body");

      // Check if the response is the expected one.
      if (*response_size != expected_response_size) {
        LOG(INFO) << "TPM response size mismatch. Try again: " << *response_size
                  << " != " << expected_response_size;
        sleep(1);
        continue;
      }

      bool ok = true;
      for (int i = 0; i < expected_response_size - kHeaderSize; i++) {
        ok &= (resp_body.at(i) == kExpectedResponse[kHeaderSize + i]);
      }
      if (!ok) {
        LOG(INFO) << "TPM response body mismatch. Try again.";
        sleep(1);
        continue;
      }

      LOG(INFO) << "TPM initialized successfully for Ti50";
      return {};
    }

    return CF_ERR("Failed to initialize Ti50 emulator");
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;

  std::unique_ptr<ProxyServer> socket_proxy_;

  SharedFD ctrl_sock_;
  SharedFD gpio_sock_;
};
}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig, LogTeeCreator,
                                 const CuttlefishConfig::InstanceSpecific>>
Ti50EmulatorComponent() {
  return fruit::createComponent()
      .addMultibinding<vm_manager::VmmDependencyCommand, Ti50Emulator>()
      .addMultibinding<CommandSource, Ti50Emulator>()
      .addMultibinding<SetupFeature, Ti50Emulator>();
}

}  // namespace cuttlefish
