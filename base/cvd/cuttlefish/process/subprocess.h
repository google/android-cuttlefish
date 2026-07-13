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
#pragma once

#include <sys/types.h>
#include <sys/wait.h>

#include <atomic>
#include <functional>

#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

enum class StopperResult {
  kStopFailure, /* Failed to stop the subprocess. */
  kStopCrash,   /* Attempted to stop the subprocess cleanly, but that failed. */
  kStopSuccess, /* The subprocess exited in the expected way. */
};

class Subprocess;
using SubprocessStopper = std::function<StopperResult(Subprocess*)>;
// Kills a process by sending it the SIGKILL signal.
StopperResult KillSubprocess(Subprocess* subprocess);
/* Creates a `SubprocessStopper` that first tries `nice_stopper` then falls back
 * to `KillSubprocess` if that fails. */
SubprocessStopper KillSubprocessFallback(std::function<StopperResult()>);
SubprocessStopper KillSubprocessFallback(SubprocessStopper nice_stopper);

// Keeps track of a running (sub)process. Allows to wait for its completion.
// It's an error to wait twice for the same subprocess.
class Subprocess {
 public:
  enum class StdIOChannel {
    kStdIn = 0,
    kStdOut = 1,
    kStdErr = 2,
  };

  Subprocess(pid_t pid, SubprocessStopper stopper = KillSubprocess)
      : pid_(pid), started_(pid > 0), stopper_(stopper) {}
  // The default implementation won't do because we need to reset the pid of the
  // moved object.
  Subprocess(Subprocess&&);
  ~Subprocess() = default;
  Subprocess& operator=(Subprocess&&);
  // Waits for the subprocess to complete. Returns zero if completed
  // successfully, non-zero otherwise.
  int Wait();
  // Same as waitid(2)
  int Wait(siginfo_t* infop, int options);
  // Whether the command started successfully. It only says whether the call to
  // fork() succeeded or not, it says nothing about exec or successful
  // completion of the command, that's what Wait is for.
  bool Started() const { return started_; }
  pid_t pid() const { return pid_; }
  StopperResult Stop() { return stopper_(this); }

  Result<void> SendSignal(int signal);
  Result<void> SendSignalToGroup(int signal);

 private:
  // Copy is disabled to avoid waiting twice for the same pid (the first wait
  // frees the pid, which allows the kernel to reuse it so we may end up waiting
  // for the wrong process)
  Subprocess(const Subprocess&) = delete;
  Subprocess& operator=(const Subprocess&) = delete;
  std::atomic<pid_t> pid_ = -1;
  bool started_ = false;
  SubprocessStopper stopper_;
};

}  // namespace cuttlefish
