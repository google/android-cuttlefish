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

namespace cuttlefish {
namespace {

class BluetoothConnector : public CommandSource {
 public:
  INJECT(BluetoothConnector(const CuttlefishConfig& config,
                            const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // CommandSource
  std::vector<Command> Commands() override {
    Command command(HostBinaryPath("bt_connector"));
    command.AddParameter("-bt_out=", fifos_[0]);
    command.AddParameter("-bt_in=", fifos_[1]);
    command.AddParameter("-hci_port=", config_.rootcanal_hci_port());
    command.AddParameter("-link_port=", config_.rootcanal_link_port());
    command.AddParameter("-test_port=", config_.rootcanal_test_port());
    return single_element_emplace(std::move(command));
  }

  // SetupFeature
  std::string Name() const override { return "BluetoothConnector"; }
  bool Enabled() const override { return config_.enable_host_bluetooth(); }

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

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  std::vector<SharedFD> fifos_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
BluetoothConnectorComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, BluetoothConnector>()
      .addMultibinding<SetupFeature, BluetoothConnector>();
}

}  // namespace cuttlefish
