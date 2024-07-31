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

#include <unistd.h>

#include <string>
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/launch/snapshot_control_files.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<MonitorCommand> SecureEnv(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSnapshotControlFiles::Type& snapshot_control_files,
    KernelLogPipeProvider& kernel_log_pipe_provider) {
  Command command(SecureEnvBinary());
  command.AddParameter("-confui_server_fd=",
                       snapshot_control_files->confui_server_fd);
  command.AddParameter("-snapshot_control_fd=",
                       snapshot_control_files->secure_env_snapshot_control_fd);

  std::vector<std::string> fifo_paths = {
      instance.PerInstanceInternalPath("keymaster_fifo_vm.in"),
      instance.PerInstanceInternalPath("keymaster_fifo_vm.out"),
      instance.PerInstanceInternalPath("gatekeeper_fifo_vm.in"),
      instance.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
      instance.PerInstanceInternalPath("oemlock_fifo_vm.in"),
      instance.PerInstanceInternalPath("oemlock_fifo_vm.out"),
      instance.PerInstanceInternalPath("keymint_fifo_vm.in"),
      instance.PerInstanceInternalPath("keymint_fifo_vm.out"),
  };
  std::vector<SharedFD> fifos;
  for (const auto& path : fifo_paths) {
    fifos.emplace_back(CF_EXPECT(SharedFD::Fifo(path, 0660)));
  }
  command.AddParameter("-keymaster_fd_out=", fifos[0]);
  command.AddParameter("-keymaster_fd_in=", fifos[1]);
  command.AddParameter("-gatekeeper_fd_out=", fifos[2]);
  command.AddParameter("-gatekeeper_fd_in=", fifos[3]);
  command.AddParameter("-oemlock_fd_out=", fifos[4]);
  command.AddParameter("-oemlock_fd_in=", fifos[5]);
  command.AddParameter("-keymint_fd_out=", fifos[6]);
  command.AddParameter("-keymint_fd_in=", fifos[7]);

  const auto& secure_hals = CF_EXPECT(config.secure_hals());
  bool secure_keymint = secure_hals.count(SecureHal::kHostKeymintSecure) > 0;
  command.AddParameter("-keymint_impl=", secure_keymint ? "tpm" : "software");
  bool secure_gatekeeper =
      secure_hals.count(SecureHal::kHostGatekeeperSecure) > 0;
  auto gatekeeper_impl = secure_gatekeeper ? "tpm" : "software";
  command.AddParameter("-gatekeeper_impl=", gatekeeper_impl);

  bool secure_oemlock = secure_hals.count(SecureHal::kHostOemlockSecure) > 0;
  auto oemlock_impl = secure_oemlock ? "tpm" : "software";
  command.AddParameter("-oemlock_impl=", oemlock_impl);

  command.AddParameter("-kernel_events_fd=",
                       kernel_log_pipe_provider.KernelLogPipe());

  return command;
}

}  // namespace cuttlefish
