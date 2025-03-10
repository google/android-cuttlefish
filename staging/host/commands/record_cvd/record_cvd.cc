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

#include <cstdint>
#include <cstdlib>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/libs/command_util/runner/defs.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"
#include "run_cvd.pb.h"

DEFINE_int32(instance_num, cuttlefish::GetInstance(),
             "Which instance to screen record.");

DEFINE_int32(
    wait_for_launcher, 30,
    "How many seconds to wait for the launcher to respond to the status "
    "command. A value of zero means wait indefinitely.");

namespace cuttlefish {
namespace {

Result<std::string> SerializeStartScreenRecordingRequest() {
  run_cvd::ExtendedLauncherAction action_proto;
  action_proto.mutable_start_screen_recording();
  std::string serialized;
  CF_EXPECT(action_proto.SerializeToString(&serialized),
            "Failed to serialize start recording request protobuf.");
  return serialized;
}

Result<std::string> SerializeStopScreenRecordingRequest() {
  run_cvd::ExtendedLauncherAction action_proto;
  action_proto.mutable_stop_screen_recording();
  std::string serialized;
  CF_EXPECT(action_proto.SerializeToString(&serialized),
            "Failed to serialize stop recording request protobuf.");
  return serialized;
}

struct RequestInfo {
  std::string serialized_data;
  ExtendedActionType extended_action_type;
};

Result<void> RecordCvdMain(int argc, char* argv[]) {
  CF_EXPECT_EQ(argc, 2, "Expected exactly one argument with record_cvd.");

  std::string command = argv[1];
  CF_EXPECT(command == "start" || command == "stop",
            "Expected the argument to be either start or start.");

  const CuttlefishConfig* config =
      CF_EXPECT(CuttlefishConfig::Get(), "Failed to obtain config object");
  SharedFD monitor_socket = CF_EXPECT(
      GetLauncherMonitor(*config, FLAGS_instance_num, FLAGS_wait_for_launcher));

  bool is_start = command == "start";
  auto request =
      CF_EXPECT(is_start ? SerializeStartScreenRecordingRequest()
                         : SerializeStopScreenRecordingRequest(),
                "Failed to create serialized recording request proto.");
  auto action_type = is_start ? ExtendedActionType::kStartScreenRecording
                              : ExtendedActionType::kStopScreenRecording;
  auto [serialized_data, extended_type] = RequestInfo{
      .serialized_data = request, .extended_action_type = action_type};

  CF_EXPECT(
      WriteLauncherActionWithData(monitor_socket, LauncherAction::kExtended,
                                  extended_type, std::move(serialized_data)));

  LauncherResponse response = CF_EXPECT(ReadLauncherResponse(monitor_socket));
  CF_EXPECTF(response == LauncherResponse::kSuccess,
             "Received \"{}\" response from launcher monitor for \""
             "{}\" request.",
             static_cast<char>(response), command);
  LOG(INFO) << "record_cvd " << command << " was successful.";
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char* argv[]) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  cuttlefish::Result<void> result = cuttlefish::RecordCvdMain(argc, argv);
  if (!result.ok()) {
    LOG(DEBUG) << result.error().FormatForEnv();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
