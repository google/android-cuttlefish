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

#include <string>

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/files/create_or_reuse_and_drain_fifo.h"
#include "cuttlefish/host/commands/run_cvd/launch/sensors_socket_pair.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/kernel_log_pipe_provider.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<Fd> Fifo(const CuttlefishConfig::InstanceSpecific& instance,
                const std::string& internal_path) {
  return CF_EXPECT(CreateOrReuseAndDrainFifo(
      instance.PerInstanceInternalPath(internal_path), 0660));
}

}  // namespace

Result<MonitorCommand> SensorsSimulator(
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSensorsSocketPair::Type& sensors_socket_pair,
    KernelLogPipeProvider& kernel_log_pipe_provider) {
  return Command(SensorsSimulatorBinary())
      .AddParameter("--control_from_guest_fd=",
                    CF_EXPECT(Fifo(instance, "sensors_control_fifo_vm.out")))
      .AddParameter("--control_to_guest_fd=",
                    CF_EXPECT(Fifo(instance, "sensors_control_fifo_vm.in")))
      .AddParameter("--data_from_guest_fd=",
                    CF_EXPECT(Fifo(instance, "sensors_data_fifo_vm.out")))
      .AddParameter("--data_to_guest_fd=",
                    CF_EXPECT(Fifo(instance, "sensors_data_fifo_vm.in")))
      .AddParameter("--webrtc_fd=", sensors_socket_pair->webrtc_socket)
      .AddParameter("-kernel_events_fd=",
                    kernel_log_pipe_provider.KernelLogPipe())
      .AddParameter("--device_type=", static_cast<int>(instance.device_type()));
}

}  // namespace cuttlefish
