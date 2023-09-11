//
// Copyright (C) 2023 The Android Open Source Project
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

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

class SnapshotController {
 public:
  static Result<std::unique_ptr<SnapshotController>> CreateSnapshotController(
      const SharedFD& channel_to_run_cvd);
  SnapshotController(const SnapshotController&) = delete;
  SnapshotController(SnapshotController&&) = delete;
  SnapshotController& operator=(const SnapshotController&) = delete;
  SnapshotController& operator=(SnapshotController&&) = delete;

  /*
   * waits until the "suspended_" is false and returns reader lock
   */
  std::shared_lock<std::shared_mutex> WaitInitializedOrResumed();

  bool Enabled() const { return is_crosvm_; }

  Result<void> ControllerLoop();
  /*
   * TODO(kwstephenkim): move these to private when ControllerLoop()
   * actually calls them.
   */
  bool ResumeAndNotify();
  bool TrySuspend();

 private:
  SnapshotController(const SharedFD& channel_to_run_cvd, const bool is_crosvm);

  const SharedFD& channel_to_run_cvd_;
  const bool is_crosvm_;
  std::atomic<bool> suspended_;
  std::shared_mutex reader_writer_mutex_;
  std::condition_variable_any suspended_cv_;
};

}  // namespace cuttlefish
