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
  run_cvd::ExtendedLauncherAction extended_action;
  if (is_start) {
    extended_action.mutable_start_screen_recording();
  } else {
    extended_action.mutable_stop_screen_recording();
  }
  auto extended_action_type = is_start
                                  ? ExtendedActionType::kStartScreenRecording
                                  : ExtendedActionType::kStopScreenRecording;
  CF_EXPECT(RunLauncherAction(monitor_socket, extended_action_type,
                              extended_action, std::nullopt));
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
