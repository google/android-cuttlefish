//
// Copyright 2023 The Android Open Source Project
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

constexpr const size_t kBufferSize = 1024;

namespace cuttlefish {
namespace {

class NfcConnector : public CommandSource {
 public:
  INJECT(NfcConnector(const CuttlefishConfig& config,
                      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command command(TcpConnectorBinary());
    command.AddParameter("-fifo_out=", fifos_[0]);
    command.AddParameter("-fifo_in=", fifos_[1]);
    command.AddParameter("-data_port=", config_.casimir_nci_port());
    command.AddParameter("-buffer_size=", kBufferSize);
    command.AddParameter("-dump_packet_size=", 10);
    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "NfcConnector"; }
  bool Enabled() const override { return config_.enable_host_nfc_connector(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() {
    std::vector<std::string> fifo_paths = {
        instance_.PerInstanceInternalPath("nfc_fifo_vm.in"),
        instance_.PerInstanceInternalPath("nfc_fifo_vm.out"),
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
NfcConnectorComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, NfcConnector>()
      .addMultibinding<SetupFeature, NfcConnector>();
}

}  // namespace cuttlefish
