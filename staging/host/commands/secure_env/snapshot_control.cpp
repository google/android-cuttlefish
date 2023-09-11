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

#include "host/commands/secure_env/snapshot_control.h"

#include <unistd.h>

#include <mutex>
#include <thread>

#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

Result<std::unique_ptr<SnapshotController>>
SnapshotController::CreateSnapshotController(
    const SharedFD& channel_to_run_cvd) {
  auto* config = CuttlefishConfig::Get();
  CF_EXPECT(config != nullptr, "Failed to get cuttlefish config.");
  CF_EXPECT(channel_to_run_cvd->IsOpen(),
            "Failed to open suspend/resume control socket.");
  SnapshotController* new_controller =
      new SnapshotController(channel_to_run_cvd, config->IsCrosvm());
  CF_EXPECT(new_controller != nullptr,
            "Memory allocation for SnapshotController failed.");
  return std::unique_ptr<SnapshotController>(new_controller);
}

SnapshotController::SnapshotController(const SharedFD& fd, const bool is_crosvm)
    : channel_to_run_cvd_(fd), is_crosvm_(is_crosvm), suspended_{false} {}

bool SnapshotController::ResumeAndNotify() {
  std::unique_lock writer_lock(reader_writer_mutex_, std::defer_lock);
  if (!writer_lock.try_lock()) {
    return false;
  }
  suspended_ = false;
  writer_lock.unlock();
  suspended_cv_.notify_all();
  return true;
}

bool SnapshotController::TrySuspend() {
  std::unique_lock writer_lock(reader_writer_mutex_, std::defer_lock);
  if (!writer_lock.try_lock()) {
    return false;
  }
  suspended_ = true;
  return true;
}

Result<void> SnapshotController::ControllerLoop() {
  LOG(INFO) << "run_cvd connected to secure_env";
  CF_EXPECT(channel_to_run_cvd_->IsOpen());
  /* TODO(kwstephenkim): add an infinite loop that reads
   * the suspend/resume command, sets/clears the atomic
   * boolean flag accordingly, and notifies the waiting
   * threads
   */
  return {};
}

void SnapshotController::WaitInitializedOrResumed() {
  std::shared_lock reader_lock(reader_writer_mutex_);
  std::atomic<bool>* suspended_atomic_ptr = &suspended_;
  suspended_cv_.wait(reader_lock, [suspended_atomic_ptr]() {
    return !(suspended_atomic_ptr->load());
  });
}

}  // namespace cuttlefish
