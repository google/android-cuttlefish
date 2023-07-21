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

#include "host/commands/run_cvd/server_loop_impl.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/libs/command_util/runner/defs.h"
#include "host/libs/command_util/util.h"
#include "run_cvd.pb.h"

namespace cuttlefish {
namespace run_cvd_impl {

Result<void> ServerLoopImpl::HandleStartScreenRecording(
    const std::string& serialized_data) {
  run_cvd::ExtendedLauncherAction extended_action;
  CF_EXPECT(extended_action.ParseFromString(serialized_data),
            "Failed to load ExtendedLauncherAction proto.");
  CF_EXPECT_EQ(
      extended_action.actions_case(),
      run_cvd::ExtendedLauncherAction::ActionsCase::kStartScreenRecording);
  LOG(INFO) << "Sending the request to start screen recording.";

  CF_EXPECT(webrtc_recorder_.SendStartRecordingCommand(),
            "Failed to send start recording command.");
  return {};
}

Result<void> ServerLoopImpl::HandleStopScreenRecording(
    const std::string& serialized_data) {
  run_cvd::ExtendedLauncherAction extended_action;
  CF_EXPECT(extended_action.ParseFromString(serialized_data),
            "Failed to load ExtendedLauncherAction proto.");
  CF_EXPECT_EQ(
      extended_action.actions_case(),
      run_cvd::ExtendedLauncherAction::ActionsCase::kStopScreenRecording);
  LOG(INFO) << "Sending the request to stop screen recording.";

  CF_EXPECT(webrtc_recorder_.SendStopRecordingCommand(),
            "Failed to send stop recording command.");
  return {};
}

}  // namespace run_cvd_impl
}  // namespace cuttlefish
