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
namespace {

Result<SharedFD> CreateFifo(const std::string& path) {
  unlink(path.c_str());
  return SharedFD::Fifo(path, 0660);
}

}  // namespace

Result<MonitorCommand> SensorsSimulator(
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSensorsSocketPair::Type& sensors_socket_pair,
    KernelLogPipeProvider& kernel_log_pipe_provider) {
  auto control_to_guest_fd = CF_EXPECT(CreateFifo(
      instance.PerInstanceInternalPath("sensors_control_fifo_vm.in")));
  auto control_from_guest_fd = CF_EXPECT(CreateFifo(
      instance.PerInstanceInternalPath("sensors_control_fifo_vm.out")));
  auto data_to_guest_fd = CF_EXPECT(
      CreateFifo(instance.PerInstanceInternalPath("sensors_data_fifo_vm.in")));
  auto data_from_guest_fd = CF_EXPECT(
      CreateFifo(instance.PerInstanceInternalPath("sensors_data_fifo_vm.out")));
  Command command(SensorsSimulatorBinary());
  command.AddParameter("--control_from_guest_fd=", control_from_guest_fd)
      .AddParameter("--control_to_guest_fd=", control_to_guest_fd)
      .AddParameter("--data_from_guest_fd=", data_from_guest_fd)
      .AddParameter("--data_to_guest_fd=", data_to_guest_fd)
      .AddParameter("--webrtc_fd=", sensors_socket_pair->webrtc_socket)
      .AddParameter("-kernel_events_fd=",
                    kernel_log_pipe_provider.KernelLogPipe())
      .AddParameter("--device_type=", static_cast<int>(instance.device_type()));
  return command;
}

}  // namespace cuttlefish
