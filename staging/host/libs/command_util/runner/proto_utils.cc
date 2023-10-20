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

#include "host/libs/command_util/runner/proto_utils.h"

#include <functional>

#include "common/libs/utils/result.h"
#include "run_cvd.pb.h"

namespace cuttlefish {
namespace {

template <typename ChooseRequestCallback>
Result<std::string> SerializeRequestImpl(
    const std::string& request_name,
    ChooseRequestCallback&& choose_request_cb) {
  run_cvd::ExtendedLauncherAction action_proto;
  choose_request_cb(action_proto);
  std::string serialized;
  CF_EXPECTF(action_proto.SerializeToString(&serialized),
             "Failed to serialize \"{}\" Request protobuf.", request_name);
  return serialized;
}

}  // namespace

Result<std::string> SerializeSuspendRequest() {
  auto call_back = [](run_cvd::ExtendedLauncherAction& action_proto) {
    action_proto.mutable_suspend();
  };
  return CF_EXPECT(SerializeRequestImpl("Suspend", call_back));
}

Result<std::string> SerializeResumeRequest() {
  auto call_back = [](run_cvd::ExtendedLauncherAction& action_proto) {
    action_proto.mutable_resume();
  };
  return CF_EXPECT(SerializeRequestImpl("Resume", call_back));
}

Result<std::string> SerializeSnapshotTakeRequest(
    const std::string& snapshot_path) {
  auto call_back =
      [snapshot_path](run_cvd::ExtendedLauncherAction& action_proto) {
        auto* snapshot_take_request = action_proto.mutable_snapshot_take();
        snapshot_take_request->add_snapshot_path(snapshot_path);
      };
  return CF_EXPECT(SerializeRequestImpl("SnapshotTake", call_back));
}

Result<std::string> SerializeStartScreenRecordingRequest() {
  auto call_back = [](run_cvd::ExtendedLauncherAction& action_proto) {
    action_proto.mutable_start_screen_recording();
  };
  return CF_EXPECT(SerializeRequestImpl("start recording", call_back));
}

Result<std::string> SerializeStopScreenRecordingRequest() {
  auto call_back = [](run_cvd::ExtendedLauncherAction& action_proto) {
    action_proto.mutable_stop_screen_recording();
  };
  return CF_EXPECT(SerializeRequestImpl("stop recording", call_back));
}

}  // namespace cuttlefish
