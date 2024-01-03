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

#if defined(CUTTLEFISH_HOST) && defined(__linux)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

#include <string>

#ifdef CUTTLEFISH_LINUX_HOST
// Pre-define this include guard since the header that normally defines it
// fights with <android-base/logging.h>
#define ABSL_LOG_CHECK_H_
#include <sandboxed_api/sandbox2/policy.h>
#include <sandboxed_api/sandbox2/policybuilder.h>
#include <sandboxed_api/sandbox2/util/bpf_helper.h>
#endif

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/config_constants.h"
#include "host/libs/config/config_utils.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

std::string LogcatInfo(const CuttlefishConfig::InstanceSpecific& instance) {
  return "Logcat output: " + instance.logcat_path();
}

Result<MonitorCommand> LogcatReceiver(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  // Open the pipe here (from the launcher) to ensure the pipe is not deleted
  // due to the usage counters in the kernel reaching zero. If this is not
  // done and the logcat_receiver crashes for some reason the VMM may get
  // SIGPIPE.
  auto log_name = instance.logcat_pipe_name();
  auto cmd = Command(LogcatReceiverBinary())
                 .AddParameter("-log_pipe_fd=",
                               CF_EXPECT(SharedFD::Fifo(log_name, 0600)));
  if (config.host_sandbox()) {
    cmd.UnsetFromEnvironment(kCuttlefishConfigEnvVarName);
    cmd.AddEnvironmentVariable(kCuttlefishConfigEnvVarName,
                               "/cuttlefish_config.json");
    cmd.AddEnvironmentVariable("LD_LIBRARY_PATH",
                               DefaultHostArtifactsPath("lib64"));
  }
  MonitorCommand monitor_cmd = std::move(cmd);
#ifdef CUTTLEFISH_LINUX_HOST
  monitor_cmd.policy =
      sandbox2::PolicyBuilder()
          .AddDirectory(DefaultHostArtifactsPath("lib64"))
          .AddDirectory(instance.PerInstanceLogPath(""), /* is_ro= */ false)
          .AddFileAt(config.AssemblyPath("cuttlefish_config.json"),
                     "/cuttlefish_config.json")
          .AddLibrariesForBinary(LogcatReceiverBinary(),
                                 DefaultHostArtifactsPath("lib64"))
          // For dynamic linking
          .AddPolicyOnSyscall(__NR_prctl,
                              {ARG_32(0), JEQ32(PR_CAPBSET_READ, ALLOW)})
          .AllowDynamicStartup()
          .AllowExit()
          .AllowGetPIDs()
          .AllowGetRandom()
          .AllowHandleSignals()
          .AllowMmap()
          .AllowOpen()
          .AllowRead()
          .AllowReadlink()
          .AllowRestartableSequences(sandbox2::PolicyBuilder::kAllowSlowFences)
          .AllowSafeFcntl()
          .AllowSyscall(__NR_tgkill)
          .AllowWrite()
          .BuildOrDie();
#endif
  return monitor_cmd;
}

}  // namespace cuttlefish
