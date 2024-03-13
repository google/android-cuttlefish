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

SnapshotCommandHandler::SnapshotCommandHandler(SharedFD channel_to_run_cvd,
                                               SnapshotSockets snapshot_sockets)
    : channel_to_run_cvd_(channel_to_run_cvd),
      snapshot_sockets_(std::move(snapshot_sockets)) {
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

Result<void> SnapshotCommandHandler::SuspendResumeHandler() {
  using ActionsCase =
      ::cuttlefish::run_cvd::ExtendedLauncherAction::ActionsCase;

  auto launcher_action =
      CF_EXPECT(ReadLauncherActionFromFd(channel_to_run_cvd_),
                "Failed to read LauncherAction from run_cvd");
  CF_EXPECT(launcher_action.action == LauncherAction::kExtended);

  switch (launcher_action.extended_action.actions_case()) {
    case ActionsCase::kSuspend: {
      LOG(DEBUG) << "Handling suspended...";
      // Request all worker threads to suspend.
      CF_EXPECT(WriteSuspendRequest(snapshot_sockets_.rust));
      CF_EXPECT(WriteSuspendRequest(snapshot_sockets_.keymaster));
      CF_EXPECT(WriteSuspendRequest(snapshot_sockets_.gatekeeper));
      CF_EXPECT(WriteSuspendRequest(snapshot_sockets_.oemlock));
      // Wait for ACKs from worker threads.
      CF_EXPECT(ReadSuspendAck(snapshot_sockets_.rust));
      CF_EXPECT(ReadSuspendAck(snapshot_sockets_.keymaster));
      CF_EXPECT(ReadSuspendAck(snapshot_sockets_.gatekeeper));
      CF_EXPECT(ReadSuspendAck(snapshot_sockets_.oemlock));
      // Write response to run_cvd.
      auto response = LauncherResponse::kSuccess;
      const auto n_written =
          channel_to_run_cvd_->Write(&response, sizeof(response));
      CF_EXPECT_EQ(sizeof(response), n_written);
      return {};
    };
    case ActionsCase::kResume: {
      LOG(DEBUG) << "Handling resume...";
      // Request all worker threads to resume.
      CF_EXPECT(WriteResumeRequest(snapshot_sockets_.rust));
      CF_EXPECT(WriteResumeRequest(snapshot_sockets_.keymaster));
      CF_EXPECT(WriteResumeRequest(snapshot_sockets_.gatekeeper));
      CF_EXPECT(WriteResumeRequest(snapshot_sockets_.oemlock));
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
