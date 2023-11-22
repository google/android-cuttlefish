//
// Copyright (C) 2023 The Android Open Source Project
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
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<std::vector<MonitorCommand>> VhostDeviceVsock(
    LogTeeCreator& log_tee,
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (!instance.vhost_user_vsock()) {
    return {};
  }
  Command command(ProcessRestarterBinary());
  command.AddParameter("-when_killed");
  command.AddParameter("-when_dumped");
  command.AddParameter("-when_exited_with_failure");
  command.AddParameter("--");
  command.AddParameter(HostBinaryPath("vhost_device_vsock"));
  command.AddEnvironmentVariable("RUST_BACKTRACE", "1");
  command.AddEnvironmentVariable("RUST_LOG", "debug");
  auto param = fmt::format(
      "guest-cid={0},socket=/tmp/vhost{0}.socket,uds-path=/tmp/vm{0}.vsock",
      instance.vsock_guest_cid());

  command.AddParameter("--vm");
  command.AddParameter(param);
  std::vector<MonitorCommand> commands;
  commands.emplace_back(std::move(
      CF_EXPECT(log_tee.CreateLogTee(command, "vhost_device_vsock"))));
  commands.emplace_back(std::move(command));
  return commands;
}

}  // namespace cuttlefish