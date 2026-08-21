/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <csignal>
#include <string>
#include <utility>

#include "absl/strings/str_format.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/vm_manager/vhost_user.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace vm_manager {

Result<VhostUserDeviceCommands> VhostUserNetDevice(
    const CuttlefishConfig& config, const std::string& name,
    const std::string& ipaddr, int prefixlen, const std::string& gateway,
    const std::string& dns) {
  const auto& instance = config.ForDefaultInstance();
  const std::string logs_path =
      instance.PerInstanceInternalPath(absl::StrFormat("passt_%s.fifo", name));
  SharedFD logs = CF_EXPECT(Fd::Fifo(logs_path, 0666));

  Command logs_cmd(HostBinaryPath("log_tee"));
  logs_cmd.AddParameter(absl::StrFormat("--process_name=passt_%s", name));
  logs_cmd.AddParameter("--log_fd_in=", logs);
  logs_cmd.SetStopper(KillSubprocessFallback([](Subprocess* proc) {
    bool res = kill(proc->pid(), SIGINT) == 0;
    return res ? StopperResult::kSuccess : StopperResult::kFailure;
  }));

  const std::string socket_path = instance.PerInstanceInternalUdsPath(
      absl::StrFormat("vhost_user_net_%s.sock", name));
  if (FileExists(socket_path)) {
    CF_EXPECT(RemoveFile(socket_path));
  }

  const std::string passt_binary = CF_EXPECT(Search(Path(), "passt"));

  Command passt_cmd(ProcessRestarterBinary());
  passt_cmd.AddParameter("-when_dumped");
  passt_cmd.AddParameter("-when_killed");
  passt_cmd.AddParameter("-when_exited_with_failure");
  passt_cmd.SetStopper(KillSubprocessFallback([](Subprocess* proc) {
    bool res = kill(proc->pid(), SIGINT) == 0;
    return res ? StopperResult::kSuccess : StopperResult::kFailure;
  }));
  passt_cmd.AddParameter("--");
  passt_cmd.AddParameter(passt_binary);
  passt_cmd.AddParameter("--vhost-user");
  passt_cmd.AddParameter("--foreground");
  passt_cmd.AddParameter("-s");
  passt_cmd.AddParameter(socket_path);
  passt_cmd.AddParameter("--repair-path");
  passt_cmd.AddParameter("none");
  passt_cmd.AddParameter("-4");
  passt_cmd.AddParameter("-a");
  passt_cmd.AddParameter(ipaddr);
  passt_cmd.AddParameter("-n");
  passt_cmd.AddParameter(prefixlen);
  passt_cmd.AddParameter("-g");
  passt_cmd.AddParameter(gateway);
  passt_cmd.AddParameter("--map-host-loopback");
  passt_cmd.AddParameter(gateway);
  if (!dns.empty()) {
    passt_cmd.AddParameter("-D");
    passt_cmd.AddParameter(dns);
  }
  passt_cmd.RedirectStdIO(Command::StdIoChannel::kStdOut, logs);
  passt_cmd.RedirectStdIO(Command::StdIoChannel::kStdErr, logs);

  return (VhostUserDeviceCommands){
      .device_cmd = std::move(passt_cmd),
      .device_logs_cmd = std::move(logs_cmd),
      .socket_path = socket_path,
  };
}

}  // namespace vm_manager
}  // namespace cuttlefish
