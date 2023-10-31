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

#include "host/commands/secure_env/suspend_resume_handler.h"

#include <android-base/logging.h>

#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

SnapshotCommandHandler::~SnapshotCommandHandler() { Join(); }

void SnapshotCommandHandler::Join() {
  if (handler_thread_.joinable()) {
    handler_thread_.join();
  }
}

SnapshotCommandHandler::SnapshotCommandHandler(
    SharedFD channel_to_run_cvd, EventFdsManager& event_fds_manager,
    EventNotifiers& suspended_notifiers, SnapshotRunningFlag& running)
    : channel_to_run_cvd_(channel_to_run_cvd),
      event_fds_manager_(event_fds_manager),
      suspended_notifiers_(suspended_notifiers),
      shared_running_(running) {
  handler_thread_ = std::thread([this]() {
    while (true) {
      auto result = SuspendResumeHandler();
      if (!result.ok()) {
        LOG(ERROR) << result.error().Trace();
        return;
      }
    }
  });
}

Result<ExtendedActionType> SnapshotCommandHandler::ReadRunCvdSnapshotCmd()
    const {
  CF_EXPECT(channel_to_run_cvd_->IsOpen(), channel_to_run_cvd_->StrError());
  auto launcher_action =
      CF_EXPECT(ReadLauncherActionFromFd(channel_to_run_cvd_),
                "Failed to read LauncherAction from run_cvd");
  CF_EXPECT(launcher_action.action == LauncherAction::kExtended);
  const auto action_type = launcher_action.type;
  CF_EXPECTF(action_type == ExtendedActionType::kSuspend ||
                 action_type == ExtendedActionType::kResume,
             "Unsupported ExtendedActionType \"{}\"", action_type);
  return action_type;
}

Result<void> SnapshotCommandHandler::SuspendResumeHandler() {
  const auto snapshot_cmd = CF_EXPECT(ReadRunCvdSnapshotCmd());
  switch (snapshot_cmd) {
    case ExtendedActionType::kSuspend: {
      LOG(DEBUG) << "Handling suspended...";
      shared_running_.UnsetRunning();  // running := false
      CF_EXPECT(event_fds_manager_.SuspendKeymasterResponder());
      CF_EXPECT(event_fds_manager_.SuspendGatekeeperResponder());
      CF_EXPECT(event_fds_manager_.SuspendOemlockResponder());
      suspended_notifiers_.keymaster_suspended_.WaitAndReset();
      suspended_notifiers_.gatekeeper_suspended_.WaitAndReset();
      suspended_notifiers_.oemlock_suspended_.WaitAndReset();
      auto response = LauncherResponse::kSuccess;
      const auto n_written =
          channel_to_run_cvd_->Write(&response, sizeof(response));
      CF_EXPECT_EQ(sizeof(response), n_written);
      return {};
    };
    case ExtendedActionType::kResume: {
      LOG(DEBUG) << "Handling resume...";
      shared_running_.SetRunning();  // running := true, and notifies all
      auto response = LauncherResponse::kSuccess;
      const auto n_written =
          channel_to_run_cvd_->Write(&response, sizeof(response));
      CF_EXPECT_EQ(sizeof(response), n_written);
      return {};
    };
    default:
      return CF_ERR("Unsupported run_cvd snapshot command.");
  }
}

}  // namespace cuttlefish
