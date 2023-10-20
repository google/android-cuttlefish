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

#include <sstream>

#include "host/libs/command_util/util.h"

namespace cuttlefish {

SnapshotCommandHandler::~SnapshotCommandHandler() { Join(); }

void SnapshotCommandHandler::Join() {
  if (handler_thread_.joinable()) {
    handler_thread_.join();
  }
}

SnapshotCommandHandler::SnapshotCommandHandler(SharedFD channel_to_run_cvd,
                                               std::atomic<bool>& running)
    : channel_to_run_cvd_(channel_to_run_cvd), shared_running_(running) {
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
  auto launcher_action =
      CF_EXPECT(ReadLauncherActionFromFd(channel_to_run_cvd_),
                "Failed to read LauncherAction from run_cvd");
  CF_EXPECT(launcher_action.action == LauncherAction::kExtended);
  return launcher_action.type;
}

Result<void> SnapshotCommandHandler::SuspendResumeHandler() {
  CF_EXPECT(shared_running_.load(), "running_ is not set as true");
  const auto snapshot_cmd = CF_EXPECT(ReadRunCvdSnapshotCmd());
  switch (snapshot_cmd) {
    case ExtendedActionType::kSuspend: {
      // TODO(kwstephenkim): implement suspend handler
      auto response = LauncherResponse::kSuccess;
      LOG(INFO) << "secure_env received the suspend command";
      const auto n_written =
          channel_to_run_cvd_->Write(&response, sizeof(response));
      CF_EXPECT_EQ(sizeof(response), n_written);
      return {};
    };
    case ExtendedActionType::kResume: {
      // TODO(kwstephenkim): implement resume handler
      auto response = LauncherResponse::kSuccess;
      LOG(INFO) << "secure_env received the resume command";
      const auto n_written =
          channel_to_run_cvd_->Write(&response, sizeof(response));
      CF_EXPECT_EQ(sizeof(response), n_written);
      return {};
    };
    default:
      std::stringstream error_msg;
      error_msg << "Unsupported run_cvd ExtendedActionType: "
                << static_cast<std::uint32_t>(snapshot_cmd);
      return CF_ERR(error_msg.str());
  }
}

}  // namespace cuttlefish
