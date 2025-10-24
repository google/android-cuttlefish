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

#include "cuttlefish/host/graphics_detector/subprocess.h"

#include <dlfcn.h>
#include <poll.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace gfxstream {
namespace {

class ScopedCloser {
 public:
  ScopedCloser(std::function<void()> func)
      : mFunc(std::move(func)), mEnabled(true) {}

  ~ScopedCloser() {
    if (mEnabled) {
      mFunc();
    }
  }

  void Disable() { mEnabled = false; }

 private:
  std::function<void()> mFunc;
  bool mEnabled = false;
};

int PidfdOpen(pid_t pid) {
  // There is no glibc wrapper for pidfd_open.
#ifndef SYS_pidfd_open
  constexpr int SYS_pidfd_open = 434;
#endif
  return syscall(SYS_pidfd_open, pid, /*flags=*/0);
}

gfxstream::expected<Ok, std::string> WaitForChild(pid_t pid) {
  siginfo_t info;
  if (TEMP_FAILURE_RETRY(waitid(P_PID, pid, &info, WEXITED | WNOWAIT)) != 0) {
    return gfxstream::unexpected("Error from waitid(): " +
                                 std::string(strerror(errno)));
  }
  if (info.si_pid != pid) {
    return gfxstream::unexpected(
        "Error from waitid(): returned different pid.");
  }
  if (info.si_code != CLD_EXITED) {
    return gfxstream::unexpected(
        "Failed to wait for subprocess: terminated by signal " +
        std::to_string(info.si_status));
  }
  return Ok{};
}

// When `pidfd_open` is not available, fallback to using a second
// thread to kill the child process after the given timeout.
gfxstream::expected<Ok, std::string> WaitForChildWithTimeoutFallback(
    pid_t pid, std::chrono::milliseconds timeout) {
  bool childExited = false;
  bool childTimedOut = false;
  std::condition_variable cv;
  std::mutex m;

  std::thread wait_thread([&]() {
    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, timeout, [&] { return childExited; })) {
      childTimedOut = true;
      kill(pid, SIGKILL);
    }
  });

  auto result = WaitForChild(pid);
  {
    std::unique_lock<std::mutex> lock(m);
    childExited = true;
  }
  cv.notify_all();
  wait_thread.join();

  if (childTimedOut) {
    return gfxstream::unexpected("Failed to wait for subprocess: timed out.");
  }
  return result;
}

gfxstream::expected<Ok, std::string> WaitForChildWithTimeout(
    pid_t pid, int pidfd, std::chrono::milliseconds timeout) {
  ScopedCloser cleanup([&]() {
    kill(pid, SIGKILL);
    WaitForChild(pid);
  });

  struct pollfd poll_info = {
      .fd = pidfd,
      .events = POLLIN,
  };
  int ret = TEMP_FAILURE_RETRY(poll(&poll_info, 1, timeout.count()));
  close(pidfd);

  if (ret < 0) {
    return gfxstream::unexpected(
        "Failed to wait for subprocess: poll() returned " +
        std::to_string(ret));
  }
  if (ret == 0) {
    return gfxstream::unexpected(
        "Failed to wait for subprocess: subprocess did not "
        "finished within " +
        std::to_string(timeout.count()) + "ms.");
  }

  cleanup.Disable();
  return WaitForChild(pid);
}

}  // namespace

gfxstream::expected<Ok, std::string> DoWithSubprocessCheck(
    const std::function<gfxstream::expected<Ok, std::string>()>& function,
    std::chrono::milliseconds timeout) {
  pid_t pid = fork();
  if (pid == 0) {
    function();
    _exit(0);
  }

  int pidfd = PidfdOpen(pid);
  if (pidfd >= 0) {
    GFXSTREAM_EXPECT(WaitForChildWithTimeout(pid, pidfd, timeout));
  } else {
    GFXSTREAM_EXPECT(WaitForChildWithTimeoutFallback(pid, timeout));
  }

  return function();
}

}  // namespace gfxstream
