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

#include <unordered_set>
#include <vector>

#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

// Copied from net/bluetooth/hci.h
#define HCI_MAX_ACL_SIZE 1024
#define HCI_MAX_FRAME_SIZE (HCI_MAX_ACL_SIZE + 4)

// Include H4 header byte, and reserve more buffer size in the case of
// excess packet.
constexpr const size_t kBufferSize = (HCI_MAX_FRAME_SIZE + 1) * 2;

class RootCanal : public CommandSource {
 public:
  INJECT(RootCanal(const CuttlefishConfig& config,
                   const CuttlefishConfig::InstanceSpecific& instance,
                   LogTeeCreator& log_tee))
      : config_(config), instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  Result<std::vector<Command>> Commands() override {
    if (!Enabled()) {
      return {};
    }

    // Create the root-canal command with the process_restarter
    // as runner to restart root-canal when it crashes.
    Command rootcanal(HostBinaryPath("process_restarter"));
    rootcanal.AddParameter("-when_killed");
    rootcanal.AddParameter("-when_dumped");
    rootcanal.AddParameter("-when_exited_with_failure");
    rootcanal.AddParameter("--");
    rootcanal.AddParameter(RootCanalBinary());

    // Configure TCP ports
    rootcanal.AddParameter("--test_port=", config_.rootcanal_test_port());
    rootcanal.AddParameter("--hci_port=", config_.rootcanal_hci_port());
    rootcanal.AddParameter("--link_port=", config_.rootcanal_link_port());
    rootcanal.AddParameter("--link_ble_port=",
                           config_.rootcanal_link_ble_port());

    // Bluetooth controller properties file
    rootcanal.AddParameter("--controller_properties_file=",
                           config_.rootcanal_config_file());
    // Default rootcanals file
    rootcanal.AddParameter("--default_commands_file=",
                           config_.rootcanal_default_commands_file());

    // Add parameters from passthrough option --rootcanal-args
    for (auto const& arg : config_.rootcanal_args()) {
      rootcanal.AddParameter(arg);
    }

    // Create the TCP connector command to open the HCI port.
    Command tcp_connector(HostBinaryPath("tcp_connector"));
    tcp_connector.AddParameter("-fifo_out=", fifos_[0]);
    tcp_connector.AddParameter("-fifo_in=", fifos_[1]);
    tcp_connector.AddParameter("-data_port=", config_.rootcanal_hci_port());
    tcp_connector.AddParameter("-buffer_size=", kBufferSize);

    // Return all commands.
    std::vector<Command> commands;
    commands.emplace_back(log_tee_.CreateLogTee(rootcanal, "rootcanal"));
    commands.emplace_back(std::move(rootcanal));
    commands.emplace_back(std::move(tcp_connector));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "RootCanal"; }
  bool Enabled() const override { return instance_.start_rootcanal(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }

  Result<void> ResultSetup() {
    std::vector<std::string> fifo_paths = {
        instance_.PerInstanceInternalPath("bt_fifo_vm.in"),
        instance_.PerInstanceInternalPath("bt_fifo_vm.out"),
    };
    for (const auto& path : fifo_paths) {
      unlink(path.c_str());
      CF_EXPECT(mkfifo(path.c_str(), 0660) == 0, "Could not create " << path);
      auto fd = SharedFD::Open(path, O_RDWR);
      CF_EXPECT(fd->IsOpen(),
                "Could not open " << path << ": " << fd->StrError());
      fifos_.push_back(fd);
    }
    return {};
  }

  bool Setup() override { return true; }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  std::vector<SharedFD> fifos_;
  LogTeeCreator& log_tee_;
};

}  // namespace

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
RootCanalComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, RootCanal>()
      .addMultibinding<SetupFeature, RootCanal>();
}

}  // namespace cuttlefish
