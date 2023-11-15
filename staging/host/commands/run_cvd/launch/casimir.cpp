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
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<std::vector<MonitorCommand>> Casimir(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance,
    LogTeeCreator& log_tee) {
  if (!(config.enable_host_nfc() && instance.start_casimir())) {
    return {};
  }

  Command command(ProcessRestarterBinary());
  command.AddParameter("-when_killed");
  command.AddParameter("-when_dumped");
  command.AddParameter("-when_exited_with_failure");
  command.AddParameter("--");

  command.AddParameter(CasimirBinary());
  command.AddParameter("--nci-port");
  command.AddParameter(config.casimir_nci_port());
  command.AddParameter("--rf-port");
  command.AddParameter(config.casimir_rf_port());
  for (auto const& arg : config.casimir_args()) {
    command.AddParameter(arg);
  }

  std::vector<MonitorCommand> commands;
  commands.emplace_back(CF_EXPECT(log_tee.CreateLogTee(command, "casimir")));
  commands.emplace_back(std::move(command));
  return commands;
}

}  // namespace cuttlefish
