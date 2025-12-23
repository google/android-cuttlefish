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

#include "cuttlefish/host/commands/run_cvd/launch/screen_recording_server.h"

#include <optional>

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::optional<MonitorCommand>> ScreenRecordingServer(
    GrpcSocketCreator& grpc_socket) {
  Command screen_recording_server_cmd(ScreenRecordingServerBinary());
  screen_recording_server_cmd.AddParameter(
      "-grpc_uds_path=", grpc_socket.CreateGrpcSocket("ScreenRecordingServer"));
  return screen_recording_server_cmd;
}

}  // namespace cuttlefish
