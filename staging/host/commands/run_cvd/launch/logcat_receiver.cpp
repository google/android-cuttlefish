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

#include <string>

#ifdef CUTTLEFISH_LINUX_HOST
// Pre-define this include guard since the header that normally defines it
// fights with <android-base/logging.h>
#define ABSL_LOG_CHECK_H_ /* this header fights with android-base/logging.h */
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#endif
#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

std::string LogcatInfo(const CuttlefishConfig::InstanceSpecific& instance) {
  return "Logcat output: " + instance.logcat_path();
}

Result<MonitorCommand> LogcatReceiver(
    const CuttlefishConfig::InstanceSpecific& instance) {
  // Open the pipe here (from the launcher) to ensure the pipe is not deleted
  // due to the usage counters in the kernel reaching zero. If this is not
  // done and the logcat_receiver crashes for some reason the VMM may get
  // SIGPIPE.
  auto log_name = instance.logcat_pipe_name();
  MonitorCommand cmd =
      Command(LogcatReceiverBinary())
          .AddParameter("-log_pipe_fd=",
                        CF_EXPECT(SharedFD::Fifo(log_name, 0600)));
#ifdef CUTTLEFISH_LINUX_HOST
  cmd.policy = sandbox2::PolicyBuilder()
                   .DangerDefaultAllowAll()
                   .DisableNamespaces()
                   .BuildOrDie();
#endif
  return cmd;
}

}  // namespace cuttlefish
