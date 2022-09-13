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

#include <android-base/logging.h>
#include <android-base/parseint.h>

#include "common/libs/utils/result.h"
#include "host/libs/config/logging.h"

namespace cuttlefish {
namespace {

Result<int> RunProcessRestarter(const char* exit_code_string,
                                const char* exec_filepath, char** exec_args) {
  LOG(VERBOSE) << "process_restarter starting";
  int restart_exit_code;
  CF_EXPECT(android::base::ParseInt(exit_code_string, &restart_exit_code),
            "Unable to parse exit code as int" << exit_code_string);
  siginfo_t infop;
  do {
    LOG(VERBOSE) << "Starting monitored process " << exec_filepath;
    pid_t pid = fork();
    CF_EXPECT(pid != -1, "fork failed (" << strerror(errno) << ")");
    if (pid == 0) {                     // child process
      prctl(PR_SET_PDEATHSIG, SIGHUP);  // Die when parent dies
      execvp(exec_filepath, exec_args);
      // if exec returns, it failed
      return CF_ERRNO("exec failed (" << strerror(errno) << ")");
    } else {  // parent process
      int return_val = TEMP_FAILURE_RETRY(waitid(P_PID, pid, &infop, WEXITED));
      CF_EXPECT(return_val != -1,
                "waitid call failed (" << strerror(errno) << ")");
      LOG(VERBOSE) << exec_filepath
                   << " exited with exit code: " << infop.si_status;
    }
  } while (infop.si_code == CLD_EXITED && infop.si_status == restart_exit_code);
  return {infop.si_status};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  // these stderr logs are directed to log tee and logged at the proper level
  ::android::base::SetMinimumLogSeverity(android::base::VERBOSE);
  if (argc < 3) {
    LOG(ERROR) << argc << " arguments provided, expected at least:"
               << " <filename> <restart_exit_code> <crosvm>";
    return EXIT_FAILURE;
  }
  auto result = cuttlefish::RunProcessRestarter(argv[1], argv[2], argv + 2);
  if (!result.ok()) {
    LOG(ERROR) << result.error();
    return EXIT_FAILURE;
  }
  return result.value();
}
