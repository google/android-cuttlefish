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

#include "cuttlefish/host/commands/run_cvd/launch/sensors_simulator.h"

#include <unistd.h>

#include <string>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/run_cvd/launch/sensors_socket_pair.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/kernel_log_pipe_provider.h"

namespace cuttlefish {

Result<MonitorCommand> SensorsSimulator(
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSensorsSocketPair::Type& sensors_socket_pair,
    KernelLogPipeProvider& kernel_log_pipe_provider) {
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
      .AddParameter("--webrtc_fd=", sensors_socket_pair->webrtc_socket)
      .AddParameter("-kernel_events_fd=",
                    kernel_log_pipe_provider.KernelLogPipe())
      .AddParameter("--device_type=", static_cast<int>(instance.device_type()));
  return command;
}

}  // namespace cuttlefish
