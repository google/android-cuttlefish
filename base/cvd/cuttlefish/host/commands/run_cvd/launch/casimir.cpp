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

#include "cuttlefish/host/commands/run_cvd/launch/casimir.h"

#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"

namespace cuttlefish {

Result<std::vector<MonitorCommand>> Casimir(
    const CuttlefishConfig& config,
    const CuttlefishConfig::EnvironmentSpecific& environment,
    const CuttlefishConfig::InstanceSpecific& instance,
    LogTeeCreator& log_tee) {
  if (!(config.enable_host_nfc() && instance.start_casimir())) {
    return {};
  }

  SharedFD nci_server = SharedFD::SocketLocalServer(
      environment.casimir_nci_socket_path(), false, SOCK_STREAM, 0600);
  CF_EXPECTF(nci_server->IsOpen(), "{}", nci_server->StrError());

  SharedFD rf_server = SharedFD::SocketLocalServer(
      environment.casimir_rf_socket_path(), false, SOCK_STREAM, 0600);
  CF_EXPECTF(rf_server->IsOpen(), "{}", rf_server->StrError());

  Command casimir = Command(ProcessRestarterBinary())
                        .AddParameter("-when_killed")
                        .AddParameter("-when_dumped")
                        .AddParameter("-when_exited_with_failure")
                        .AddParameter("--")
                        .AddParameter(CasimirBinary())
                        .AddParameter("--nci-unix-fd")
                        .AddParameter(nci_server)
                        .AddParameter("--rf-unix-fd")
                        .AddParameter(rf_server);

  for (auto const& arg : config.casimir_args()) {
    casimir.AddParameter(arg);
  }

  std::vector<MonitorCommand> commands;
  commands.emplace_back(
      CF_EXPECT(log_tee.CreateFullLogTee(casimir, "casimir")));
  commands.emplace_back(std::move(casimir));
  return commands;
}

}  // namespace cuttlefish
