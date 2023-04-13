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
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class RootCanal : public CommandSource {
 public:
  INJECT(RootCanal(const CuttlefishConfig& config,
                   const CuttlefishConfig::InstanceSpecific& instance,
                   LogTeeCreator& log_tee))
      : config_(config), instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    // Create the root-canal command with the process_restarter
    // as runner to restart root-canal when it crashes.
    Command command(ProcessRestarterBinary());
    command.AddParameter("-when_killed");
    command.AddParameter("-when_dumped");
    command.AddParameter("-when_exited_with_failure");
    command.AddParameter("--");
    command.AddParameter(RootCanalBinary());

    // Test port
    command.AddParameter("--test_port=", config_.rootcanal_test_port());
    // HCI server port
    command.AddParameter("--hci_port=", config_.rootcanal_hci_port());
    // Link server port
    command.AddParameter("--link_port=", config_.rootcanal_link_port());
    // Link ble server port
    command.AddParameter("--link_ble_port=", config_.rootcanal_link_ble_port());
    // Bluetooth controller properties file
    command.AddParameter("--controller_properties_file=",
                         config_.rootcanal_config_file());
    // Default commands file
    command.AddParameter("--default_commands_file=",
                         config_.rootcanal_default_commands_file());

    // Add parameters from passthrough option --rootcanal-args
    for (auto const& arg : config_.rootcanal_args()) {
      command.AddParameter(arg);
    }

    std::vector<MonitorCommand> commands;
    commands.emplace_back(
        std::move(log_tee_.CreateLogTee(command, "rootcanal")));
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "RootCanal"; }
  bool Enabled() const override {
    return config_.enable_host_bluetooth_connector() && instance_.start_rootcanal();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
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
