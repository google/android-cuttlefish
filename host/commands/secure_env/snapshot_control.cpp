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

#include <thread>

#include "common/libs/fs/shared_buf.h"
#include "host/libs/command_util/runner/defs.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"
#include "run_cvd.pb.h"

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
  while (true) {
    auto launcher_action =
        CF_EXPECT(ReadLauncherActionFromFd(channel_to_run_cvd_),
                  "Failed to read LauncherAction from run_cvd");
    CF_EXPECT(launcher_action.action == LauncherAction::kExtended);
    const auto action_type = launcher_action.type;
    CF_EXPECTF(action_type == ExtendedActionType::kSuspend ||
                   action_type == ExtendedActionType::kResume,
               "Unsupported ExtendedActionType \"{}\"", action_type);

    auto response = LauncherResponse::kError;
    if (action_type == ExtendedActionType::kSuspend) {
      if (TrySuspend()) {
        response = LauncherResponse::kSuccess;
      }
    } else {
      if (ResumeAndNotify()) {
        response = LauncherResponse::kSuccess;
      }
    }
    const auto n_written =
        channel_to_run_cvd_->Write(&response, sizeof(response));
    CF_EXPECT_EQ(n_written, sizeof(response));
  }
  return {};
}

std::shared_lock<std::shared_mutex>
SnapshotController::WaitInitializedOrResumed() {
  std::shared_lock reader_lock(reader_writer_mutex_);
  std::atomic<bool>* suspended_atomic_ptr = &suspended_;
  suspended_cv_.wait(reader_lock, [suspended_atomic_ptr]() {
    return !(suspended_atomic_ptr->load());
  });
  return std::move(reader_lock);
}

}  // namespace cuttlefish
