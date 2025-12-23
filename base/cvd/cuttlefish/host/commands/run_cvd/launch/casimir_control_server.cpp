/*
 * Copyright 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/run_cvd/launch/casimir_control_server.h"

#include <optional>
#include <string>

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::optional<MonitorCommand>> CasimirControlServer(
    const CuttlefishConfig& config,
    const CuttlefishConfig::EnvironmentSpecific& environment,
    const CuttlefishConfig::InstanceSpecific& instance,
    GrpcSocketCreator& grpc_socket) {
  if (!config.enable_host_nfc()) {
    return {};
  }
  if (!instance.start_casimir()) {
    return {};
  }

  Command casimir_control_server_cmd(CasimirControlServerBinary());
  casimir_control_server_cmd.AddParameter(
      "-grpc_uds_path=", grpc_socket.CreateGrpcSocket("CasimirControlServer"));
  casimir_control_server_cmd.AddParameter("-casimir_rf_path=",
                                          environment.casimir_rf_socket_path());
  return casimir_control_server_cmd;
}

}  // namespace cuttlefish
