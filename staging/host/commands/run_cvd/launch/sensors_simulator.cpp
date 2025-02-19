//
// Copyright (C) 2025 The Android Open Source Project
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
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<MonitorCommand> SensorsSimulator(
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSensorsSocketPair::Type& sensors_socket_pair) {
  std::string to_guest_pipe_path =
      instance.PerInstanceInternalPath("sensors_fifo_vm.in");
  std::string from_guest_pipe_path =
      instance.PerInstanceInternalPath("sensors_fifo_vm.out");
  unlink(to_guest_pipe_path.c_str());
  unlink(from_guest_pipe_path.c_str());
  auto to_guest_fd = CF_EXPECT(SharedFD::Fifo(to_guest_pipe_path, 0660));
  auto from_guest_fd = CF_EXPECT(SharedFD::Fifo(from_guest_pipe_path, 0660));
  Command command(SensorsSimulatorBinary());
  command.AddParameter("--sensors_in_fd=", from_guest_fd)
      .AddParameter("--sensors_out_fd=", to_guest_fd)
      .AddParameter("--webrtc_fd=", sensors_socket_pair->webrtc_socket);
  return command;
}

}  // namespace cuttlefish