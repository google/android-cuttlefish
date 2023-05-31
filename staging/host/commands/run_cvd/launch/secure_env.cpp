//
// Copyright (C) 2019 The Android Open Source Project
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

#include "host/commands/run_cvd/launch/launch.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class SecureEnvironment : public CommandSource, public KernelLogPipeConsumer {
 public:
  INJECT(SecureEnvironment(const CuttlefishConfig& config,
                           const CuttlefishConfig::InstanceSpecific& instance,
                           KernelLogPipeProvider& kernel_log_pipe_provider))
      : config_(config),
        instance_(instance),
        kernel_log_pipe_provider_(kernel_log_pipe_provider) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command command(SecureEnvBinary());
    command.AddParameter("-confui_server_fd=", confui_server_fd_);
    command.AddParameter("-keymaster_fd_out=", fifos_[0]);
    command.AddParameter("-keymaster_fd_in=", fifos_[1]);
    command.AddParameter("-gatekeeper_fd_out=", fifos_[2]);
    command.AddParameter("-gatekeeper_fd_in=", fifos_[3]);
    command.AddParameter("-oemlock_fd_out=", fifos_[4]);
    command.AddParameter("-oemlock_fd_in=", fifos_[5]);
    command.AddParameter("-keymint_fd_out=", fifos_[6]);
    command.AddParameter("-keymint_fd_in=", fifos_[7]);

    const auto& secure_hals = config_.secure_hals();
    bool secure_keymint = secure_hals.count(SecureHal::Keymint) > 0;
    command.AddParameter("-keymint_impl=", secure_keymint ? "tpm" : "software");
    bool secure_gatekeeper = secure_hals.count(SecureHal::Gatekeeper) > 0;
    auto gatekeeper_impl = secure_gatekeeper ? "tpm" : "software";
    command.AddParameter("-gatekeeper_impl=", gatekeeper_impl);

    bool secure_oemlock = secure_hals.count(SecureHal::Oemlock) > 0;
    auto oemlock_impl = secure_oemlock ? "tpm" : "software";
    command.AddParameter("-oemlock_impl=", oemlock_impl);

    command.AddParameter("-kernel_events_fd=", kernel_log_pipe_);

    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "SecureEnvironment"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {&kernel_log_pipe_provider_};
  }
  Result<void> ResultSetup() override {
    std::vector<std::string> fifo_paths = {
        instance_.PerInstanceInternalPath("keymaster_fifo_vm.in"),
        instance_.PerInstanceInternalPath("keymaster_fifo_vm.out"),
        instance_.PerInstanceInternalPath("gatekeeper_fifo_vm.in"),
        instance_.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
        instance_.PerInstanceInternalPath("oemlock_fifo_vm.in"),
        instance_.PerInstanceInternalPath("oemlock_fifo_vm.out"),
        instance_.PerInstanceInternalPath("keymint_fifo_vm.in"),
        instance_.PerInstanceInternalPath("keymint_fifo_vm.out"),
    };
    std::vector<SharedFD> fifos;
    for (const auto& path : fifo_paths) {
      unlink(path.c_str());
      CF_EXPECT(mkfifo(path.c_str(), 0660) == 0, "Could not create " << path);
      auto fd = SharedFD::Open(path, O_RDWR);
      CF_EXPECT(fd->IsOpen(),
                "Could not open " << path << ": " << fd->StrError());
      fifos_.push_back(fd);
    }

    auto confui_socket_path =
        instance_.PerInstanceInternalPath("confui_sign.sock");
    confui_server_fd_ = SharedFD::SocketLocalServer(confui_socket_path, false,
                                                    SOCK_STREAM, 0600);
    CF_EXPECT(confui_server_fd_->IsOpen(),
              "Could not open " << confui_socket_path << ": "
                                << confui_server_fd_->StrError());
    kernel_log_pipe_ = kernel_log_pipe_provider_.KernelLogPipe();

    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD confui_server_fd_;
  std::vector<SharedFD> fifos_;
  KernelLogPipeProvider& kernel_log_pipe_provider_;
  SharedFD kernel_log_pipe_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific,
                                 KernelLogPipeProvider>>
SecureEnvComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, SecureEnvironment>()
      .addMultibinding<KernelLogPipeConsumer, SecureEnvironment>()
      .addMultibinding<SetupFeature, SecureEnvironment>();
}

}  // namespace cuttlefish
