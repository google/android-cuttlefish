/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <optional>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <gflags/gflags.h>

#include "common/libs/utils/result.h"
#include "host/libs/config/logging.h"

DEFINE_bool(when_dumped, false, "restart when the process crashed");
DEFINE_bool(when_killed, false, "restart when the process was killed");
DEFINE_bool(when_exited_with_failure, false,
            "restart when the process exited with a code !=0");
DEFINE_int32(when_exited_with_code, -1,
             "restart when the process exited with a specific code");

namespace cuttlefish {
namespace {

static bool ShouldRestartProcess(siginfo_t const& info) {
  if (info.si_code == CLD_DUMPED && FLAGS_when_dumped) {
    return true;
  }
  if (info.si_code == CLD_KILLED && FLAGS_when_killed) {
    return true;
  }
  if (info.si_code == CLD_EXITED && FLAGS_when_exited_with_failure &&
      info.si_status != 0) {
    return true;
  }
  if (info.si_code == CLD_EXITED &&
      info.si_status == FLAGS_when_exited_with_code) {
    return true;
  }
  return false;
}

Result<int> RunProcessRestarter(const char* exec_cmd, char** exec_args) {
  LOG(VERBOSE) << "process_restarter starting";
  siginfo_t infop;

  do {
    LOG(VERBOSE) << "Starting monitored process " << exec_cmd;
    pid_t pid = fork();
    CF_EXPECT(pid != -1, "fork failed (" << strerror(errno) << ")");
    if (pid == 0) {                     // child process
      prctl(PR_SET_PDEATHSIG, SIGHUP);  // Die when parent dies
      execvp(exec_cmd, exec_args);
      // if exec returns, it failed
      return CF_ERRNO("exec failed (" << strerror(errno) << ")");
    } else {  // parent process
      int return_val = TEMP_FAILURE_RETRY(waitid(P_PID, pid, &infop, WEXITED));
      CF_EXPECT(return_val != -1,
                "waitid call failed (" << strerror(errno) << ")");
      LOG(VERBOSE) << exec_cmd << " exited with exit code: " << infop.si_status;
    }
  } while (ShouldRestartProcess(infop));
  return {infop.si_status};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  // these stderr logs are directed to log tee and logged at the proper level
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  ::android::base::SetMinimumLogSeverity(android::base::VERBOSE);

  gflags::SetUsageMessage(R"#(
    This program launches and automatically restarts the input command
    following the selected restart conditions.
    Example usage:

      ./process_restarter -when_dumped -- my_program --arg1 --arg2
  )#");

  // Parse command line flags with remove_flags=true
  // so that the remainder is the command to execute.
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto result = cuttlefish::RunProcessRestarter(argv[1], argv + 1);
  if (!result.ok()) {
    LOG(ERROR) << result.error().Message();
    LOG(DEBUG) << result.error().Trace();
    return EXIT_FAILURE;
  }
  return result.value();
}
