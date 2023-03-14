/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/libs/graphics_detector/subprocess.h"

#include <condition_variable>
#include <mutex>
#include <thread>

#include <dlfcn.h>
#include <poll.h>
#include <sys/wait.h>

#include <android-base/logging.h>
#include <android-base/scopeguard.h>
#include <android-base/unique_fd.h>

namespace cuttlefish {
namespace {

int PidfdOpen(pid_t pid) {
  // There is no glibc wrapper for pidfd_open.
#ifndef SYS_pidfd_open
  constexpr int SYS_pidfd_open = 434;
#endif
  return syscall(SYS_pidfd_open, pid, /*flags=*/0);
}

SubprocessResult WaitForChild(pid_t pid) {
  siginfo_t info;

  int options = WEXITED | WNOWAIT;
  if (TEMP_FAILURE_RETRY(waitid(P_PID, pid, &info, options)) != 0) {
    PLOG(ERROR) << "Failed to wait for subprocess: waitid error.";
    return SubprocessResult::kFailure;
  }
  if (info.si_pid != pid) {
    LOG(ERROR)
        << "Failed to wait for subprocess: waitid returned different pid";
    return SubprocessResult::kFailure;
  }
  if (info.si_code != CLD_EXITED) {
    LOG(ERROR)
        << "Failed to wait for subprocess: subprocess terminated by signal "
        << info.si_status << ".";
    return SubprocessResult::kFailure;
  }
  return SubprocessResult::kSuccess;
}

SubprocessResult WaitForChildWithTimeoutFallback(
    pid_t pid, std::chrono::milliseconds timeout) {
  bool child_exited = false;
  bool child_timed_out = false;
  std::condition_variable cv;
  std::mutex m;

  std::thread wait_thread([&]() {
    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, timeout, [&] { return child_exited; })) {
      child_timed_out = true;
      if (kill(pid, SIGKILL) != 0) {
        PLOG(ERROR) << "Failed to kill subprocess after " << timeout.count()
                    << "ms timeout.";
      }
    }
  });

  SubprocessResult result = WaitForChild(pid);
  {
    std::unique_lock<std::mutex> lock(m);
    child_exited = true;
  }
  cv.notify_all();
  wait_thread.join();

  if (child_timed_out) {
    return SubprocessResult::kFailure;
  }
  return result;
}

// When `pidfd_open` is not available, fallback to using a second
// thread to kill the child process after the given timeout.
SubprocessResult WaitForChildWithTimeout(pid_t pid,
                                         android::base::unique_fd pidfd,
                                         std::chrono::milliseconds timeout) {
  auto cleanup = android::base::make_scope_guard([&]() {
    kill(pid, SIGKILL);
    WaitForChild(pid);
  });

  struct pollfd poll_info = {
      .fd = pidfd.get(),
      .events = POLLIN,
  };
  int ret = TEMP_FAILURE_RETRY(poll(&poll_info, 1, timeout.count()));
  pidfd.reset();

  if (ret < 0) {
    LOG(ERROR) << "Failed to wait for subprocess " << pid
               << ": poll failed with " << ret;
    return SubprocessResult::kFailure;
  }
  if (ret == 0) {
    LOG(ERROR) << "Subprocess did not complete within " << timeout.count()
               << "ms. Killing...";
    return SubprocessResult::kFailure;
  }

  cleanup.Disable();
  return WaitForChild(pid);
}

}  // namespace

SubprocessResult DoWithSubprocessCheck(const std::string& message,
                                       const std::function<void()>& function,
                                       std::chrono::milliseconds timeout) {
  LOG(VERBOSE) << "Running " << message << " in subprocess...";
  pid_t pid = fork();
  if (pid == 0) {
    function();
    std::exit(0);
  }

  LOG(VERBOSE) << "Waiting for subprocess " << pid << " running " << message
               << "...";

  SubprocessResult result = SubprocessResult::kFailure;

  android::base::unique_fd pidfd(PidfdOpen(pid));
  if (pidfd.get() >= 0) {
    result = WaitForChildWithTimeout(pid, std::move(pidfd), timeout);
  } else {
    result = WaitForChildWithTimeoutFallback(pid, timeout);
  }

  if (result == SubprocessResult::kSuccess) {
    LOG(VERBOSE) << "Subprocess running " << message << " succeeded. Running "
                 << message << " in this process...";
    function();
    return SubprocessResult::kSuccess;
  } else {
    LOG(VERBOSE) << "Subprocess running " << message << " failed. Not running "
                 << message << " in this process.";
    return SubprocessResult::kFailure;
  }
}

}  // namespace cuttlefish