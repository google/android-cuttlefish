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

#define UCI_HEADER_SIZE 4
#define UCI_MAX_PAYLOAD_SIZE 255
#define UCI_MAX_PACKET_SIZE (UCI_HEADER_SIZE + UCI_MAX_PAYLOAD_SIZE)

constexpr const size_t kBufferSize = UCI_MAX_PACKET_SIZE * 2;

namespace cuttlefish {
namespace {

class UwbConnector : public CommandSource {
 public:
  INJECT(UwbConnector(const CuttlefishConfig& config,
                            const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command command(HostBinaryPath("tcp_connector"));
    command.AddParameter("-fifo_out=", fifos_[0]);
    command.AddParameter("-fifo_in=", fifos_[1]);
    command.AddParameter("-data_port=", config_.pica_uci_port());
    command.AddParameter("-buffer_size=", kBufferSize);
    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "UwbConnector"; }
  bool Enabled() const override { return config_.enable_host_uwb_connector(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() {
    std::vector<std::string> fifo_paths = {
        instance_.PerInstanceInternalPath("uwb_fifo_vm.in"),
        instance_.PerInstanceInternalPath("uwb_fifo_vm.out"),
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
UwbConnectorComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, UwbConnector>()
      .addMultibinding<SetupFeature, UwbConnector>();
}

}  // namespace cuttlefish
