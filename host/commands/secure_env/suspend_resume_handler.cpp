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
namespace {

Result<void> WriteSuspendRequest(const SharedFD& socket) {
  const SnapshotSocketMessage suspend_request = SnapshotSocketMessage::kSuspend;
  CF_EXPECT_EQ(sizeof(suspend_request),
               socket->Write(&suspend_request, sizeof(suspend_request)),
               "socket write failed: " << socket->StrError());
  return {};
}

Result<void> ReadSuspendAck(const SharedFD& socket) {
  SnapshotSocketMessage ack_response;
  CF_EXPECT_EQ(sizeof(ack_response),
               socket->Read(&ack_response, sizeof(ack_response)),
               "socket read failed: " << socket->StrError());
  CF_EXPECT_EQ(SnapshotSocketMessage::kSuspendAck, ack_response);
  return {};
}

Result<void> WriteResumeRequest(const SharedFD& socket) {
  const SnapshotSocketMessage resume_request = SnapshotSocketMessage::kResume;
  CF_EXPECT_EQ(sizeof(resume_request),
               socket->Write(&resume_request, sizeof(resume_request)),
               "socket write failed: " << socket->StrError());
  return {};
}

}  // namespace

SnapshotCommandHandler::~SnapshotCommandHandler() { Join(); }

void SnapshotCommandHandler::Join() {
  if (handler_thread_.joinable()) {
    handler_thread_.join();
  }
}

SnapshotCommandHandler::SnapshotCommandHandler(
    SharedFD channel_to_run_cvd, EventFdsManager& event_fds_manager,
    EventNotifiers& suspended_notifiers, SnapshotRunningFlag& running,
    SharedFD rust_snapshot_socket)
    : channel_to_run_cvd_(channel_to_run_cvd),
      event_fds_manager_(event_fds_manager),
      suspended_notifiers_(suspended_notifiers),
      shared_running_(running),
      rust_snapshot_socket_(std::move(rust_snapshot_socket)) {
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
      // Request all worker threads to suspend.
      shared_running_.UnsetRunning();  // running := false
      CF_EXPECT(event_fds_manager_.SuspendKeymasterResponder());
      CF_EXPECT(event_fds_manager_.SuspendGatekeeperResponder());
      CF_EXPECT(event_fds_manager_.SuspendOemlockResponder());
      CF_EXPECT(WriteSuspendRequest(rust_snapshot_socket_));
      // Wait for ACKs from worker threads.
      suspended_notifiers_.keymaster_suspended_.WaitAndReset();
      suspended_notifiers_.gatekeeper_suspended_.WaitAndReset();
      suspended_notifiers_.oemlock_suspended_.WaitAndReset();
      CF_EXPECT(ReadSuspendAck(rust_snapshot_socket_));
      // Write response to run_cvd.
      auto response = LauncherResponse::kSuccess;
      const auto n_written =
          channel_to_run_cvd_->Write(&response, sizeof(response));
      CF_EXPECT_EQ(sizeof(response), n_written);
      return {};
    };
    case ExtendedActionType::kResume: {
      LOG(DEBUG) << "Handling resume...";
      // Request all worker threads to resume.
      shared_running_.SetRunning();  // running := true, and notifies all
      CF_EXPECT(WriteResumeRequest(rust_snapshot_socket_));
      // Write response to run_cvd.
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
