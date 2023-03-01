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

#include "host/commands/cvd/server_command/subprocess_waiter.h"

namespace cuttlefish {

Result<void> SubprocessWaiter::Setup(Subprocess subprocess) {
  std::unique_lock interrupt_lock(interruptible_);
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(!subprocess_, "Already running");

  subprocess_ = std::move(subprocess);
  return {};
}

Result<siginfo_t> SubprocessWaiter::Wait() {
  std::unique_lock interrupt_lock(interruptible_);
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(subprocess_.has_value());

  siginfo_t infop{};

  interrupt_lock.unlock();

  // This blocks until the process exits, but doesn't reap it.
  auto result = subprocess_->Wait(&infop, WEXITED | WNOWAIT);
  CF_EXPECT(result != -1, "Lost track of subprocess pid");
  interrupt_lock.lock();
  // Perform a reaping wait on the process (which should already have exited).
  result = subprocess_->Wait(&infop, WEXITED);
  CF_EXPECT(result != -1, "Lost track of subprocess pid");
  // The double wait avoids a race around the kernel reusing pids. Waiting
  // with WNOWAIT won't cause the child process to be reaped, so the kernel
  // won't reuse the pid until the Wait call below, and any kill signals won't
  // reach unexpected processes.

  subprocess_ = {};

  return infop;
}

Result<void> SubprocessWaiter::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  if (subprocess_) {
    auto stop_result = subprocess_->Stop();
    switch (stop_result) {
      case StopperResult::kStopFailure:
        return CF_ERR("Failed to stop subprocess");
      case StopperResult::kStopCrash:
        return CF_ERR("Stopper caused process to crash");
      case StopperResult::kStopSuccess:
        return {};
      default:
        return CF_ERR("Unknown stop result: " << (uint64_t)stop_result);
    }
  }
  return {};
}

}  // namespace cuttlefish
