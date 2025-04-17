/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/libs/vm_manager/vhost_user.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <utility>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <vulkan/vulkan.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/crosvm_builder.h"

namespace cuttlefish {
namespace vm_manager {

// TODO(schuffelen): Deduplicate with BuildVhostUserGpu
Result<VhostUserDeviceCommands> VhostUserBlockDevice(
    const CuttlefishConfig& config, int num, std::string_view disk_path) {
  const auto& instance = config.ForDefaultInstance();

  CF_EXPECT(instance.vhost_user_block(), "Feature is not enabled");

  auto block_device_socket_path = instance.PerInstanceInternalUdsPath(
      fmt::format("vhost-user-block-{}-socket", num));
  auto block_device_logs_path = instance.PerInstanceInternalPath(
      fmt::format("crosvm_vhost_user_block_{}.fifo", num));
  auto block_device_logs =
      CF_EXPECT(SharedFD::Fifo(block_device_logs_path, 0666));

  Command block_device_logs_cmd(HostBinaryPath("log_tee"));
  block_device_logs_cmd.AddParameter("--process_name=crosvm_block_", num);
  block_device_logs_cmd.AddParameter("--log_fd_in=", block_device_logs);
  block_device_logs_cmd.SetStopper(KillSubprocessFallback([](Subprocess* proc) {
    // Ask nicely so that log_tee gets a chance to process all the logs.
    // TODO: b/335934714 - Make sure the process actually exits
    bool res = kill(proc->pid(), SIGINT) == 0;
    return res ? StopperResult::kStopSuccess : StopperResult::kStopFailure;
  }));

  const std::string crosvm_path = config.crosvm_binary();

  CrosvmBuilder block_device_cmd;

  // NOTE: The "main" crosvm process returns a kCrosvmVmResetExitCode when the
  // guest exits but the "block" crosvm just exits cleanly with 0 after the
  // "main" crosvm disconnects.
  block_device_cmd.ApplyProcessRestarter(config.crosvm_binary(),
                                         /*first_time_argument=*/"",
                                         /*exit_code=*/0);

  block_device_cmd.Cmd().AddParameter("devices");
  block_device_cmd.Cmd().AddParameter("--block");
  block_device_cmd.Cmd().AddParameter("vhost=", block_device_socket_path,
                                      ",path=", disk_path);

  if (instance.enable_sandbox()) {
    const bool seccomp_exists = DirectoryExists(instance.seccomp_policy_dir());
    const std::string& var_empty_dir = kCrosvmVarEmptyDir;
    const bool var_empty_available = DirectoryExists(var_empty_dir);
    CF_EXPECT(var_empty_available && seccomp_exists,
              var_empty_dir << " is not an existing, empty directory."
                            << "seccomp-policy-dir, "
                            << instance.seccomp_policy_dir()
                            << " does not exist");
    block_device_cmd.Cmd().AddParameter("--jail");
    block_device_cmd.Cmd().AddParameter("seccomp-policy-dir=",
                                        instance.seccomp_policy_dir());
  } else {
    block_device_cmd.Cmd().AddParameter("--disable-sandbox");
  }

  return (VhostUserDeviceCommands){
      .device_cmd = std::move(block_device_cmd.Cmd()),
      .device_logs_cmd = std::move(block_device_logs_cmd),
      .socket_path = block_device_socket_path,
  };
}

}  // namespace vm_manager
}  // namespace cuttlefish
