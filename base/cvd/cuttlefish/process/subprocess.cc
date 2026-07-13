/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "cuttlefish/process/subprocess.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <functional>

#include "absl/log/log.h"

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

extern char** environ;

namespace cuttlefish {

Subprocess::Subprocess(Subprocess&& subprocess)
    : pid_(subprocess.pid_.load()),
      started_(subprocess.started_),
      stopper_(subprocess.stopper_) {
  // Make sure the moved object no longer controls this subprocess
  subprocess.pid_ = -1;
  subprocess.started_ = false;
}

Subprocess& Subprocess::operator=(Subprocess&& other) {
  pid_ = other.pid_.load();
  started_ = other.started_;
  stopper_ = other.stopper_;

  other.pid_ = -1;
  other.started_ = false;
  return *this;
}

int Subprocess::Wait() {
  if (pid_ < 0) {
    LOG(ERROR)
        << "Attempt to wait on invalid pid(has it been waited on already?): "
        << pid_;
    return -1;
  }
  int wstatus = 0;
  auto pid = pid_.load();  // Wait will set pid_ to -1 after waiting
  auto wait_ret = waitpid(pid, &wstatus, 0);
  if (wait_ret < 0) {
    auto error = errno;
    LOG(ERROR) << "Error on call to waitpid: " << strerror(error);
    return wait_ret;
  }
  int retval = 0;
  if (WIFEXITED(wstatus)) {
    pid_ = -1;
    retval = WEXITSTATUS(wstatus);
    if (retval) {
      VLOG(0) << "Subprocess " << pid << " exited with error code: " << retval;
    }
  } else if (WIFSIGNALED(wstatus)) {
    pid_ = -1;
    int sig_num = WTERMSIG(wstatus);
    LOG(ERROR) << "Subprocess " << pid << " was interrupted by a signal '"
               << strsignal(sig_num) << "' (" << sig_num << ")";
    retval = -1;
  }
  return retval;
}
// NOLINTNEXTLINE(misc-include-cleaner): <signal.h>
int Subprocess::Wait(siginfo_t* infop, int options) {
  if (pid_ < 0) {
    LOG(ERROR)
        << "Attempt to wait on invalid pid(has it been waited on already?): "
        << pid_;
    return -1;
  }
  *infop = {};
  // NOLINTNEXTLINE(misc-include-cleaner): <sys/wait.h>
  auto retval = TEMP_FAILURE_RETRY(waitid(P_PID, pid_, infop, options));
  // We don't want to wait twice for the same process
  bool exited = infop->si_code == CLD_EXITED || infop->si_code == CLD_DUMPED;
  bool reaped = !(options & WNOWAIT);
  if (exited && reaped) {
    pid_ = -1;
  }
  return retval;
}

static Result<void> SendSignalImpl(const int signal, const pid_t pid,
                                   bool to_group, const bool started) {
  if (pid == -1) {
    return CF_ERR(strerror(ESRCH));
  }
  CF_EXPECTF(started == true,
             "The Subprocess object lost the ownership"
             "of the process {}.",
             pid);
  int ret_code = 0;
  if (to_group) {
    ret_code = killpg(getpgid(pid), signal);
  } else {
    ret_code = kill(pid, signal);
  }
  CF_EXPECTF(ret_code == 0, "kill/killpg returns {} with errno: {}", ret_code,
             strerror(errno));
  return {};
}

Result<void> Subprocess::SendSignal(const int signal) {
  CF_EXPECT(SendSignalImpl(signal, pid_, /* to_group */ false, started_));
  return {};
}

Result<void> Subprocess::SendSignalToGroup(const int signal) {
  CF_EXPECT(SendSignalImpl(signal, pid_, /* to_group */ true, started_));
  return {};
}

StopperResult KillSubprocess(Subprocess* subprocess) {
  auto pid = subprocess->pid();
  if (pid > 0) {
    auto pgid = getpgid(pid);
    if (pgid < 0) {
      auto error = errno;
      LOG(WARNING) << "Error obtaining process group id of process with pid="
                   << pid << ": " << strerror(error);
      // Send the kill signal anyways, because pgid will be -1 it will be sent
      // to the process and not a (non-existent) group
    }
    bool is_group_head = pid == pgid;
    auto kill_ret = (is_group_head ? killpg : kill)(pid, SIGKILL);
    if (kill_ret == 0) {
      return StopperResult::kStopSuccess;
    }
    auto kill_cmd = is_group_head ? "killpg(" : "kill(";
    PLOG(ERROR) << kill_cmd << pid << ", SIGKILL) failed: ";
    return StopperResult::kStopFailure;
  }
  return StopperResult::kStopSuccess;
}

SubprocessStopper KillSubprocessFallback(std::function<StopperResult()> nice) {
  return KillSubprocessFallback([nice](Subprocess*) { return nice(); });
}

SubprocessStopper KillSubprocessFallback(SubprocessStopper nice_stopper) {
  return [nice_stopper](Subprocess* process) {
    auto nice_result = nice_stopper(process);
    if (nice_result == StopperResult::kStopFailure) {
      auto harsh_result = KillSubprocess(process);
      return harsh_result == StopperResult::kStopSuccess
                 ? StopperResult::kStopCrash
                 : harsh_result;
    }
    return nice_result;
  };
}

}  // namespace cuttlefish
