/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_int32(instance_num, cuttlefish::GetInstance(),
             "Which instance to powerwash.");

DEFINE_int32(
    wait_for_launcher, 30,
    "How many seconds to wait for the launcher to respond to the status "
    "command. A value of zero means wait indefinitely.");

DEFINE_int32(boot_timeout, 360, "How many seconds to wait for the device to "
                                 "reboot.");

namespace cuttlefish {
namespace {

Result<void> PowerwashCvdMain() {
  const CuttlefishConfig* config =
      CF_EXPECT(CuttlefishConfig::Get(), "Failed to obtain config object");
  SharedFD monitor_socket = CF_EXPECT(
      GetLauncherMonitor(*config, FLAGS_instance_num, FLAGS_wait_for_launcher));

  LOG(INFO) << "Requesting powerwash";
  CF_EXPECT(WriteLauncherAction(monitor_socket, LauncherAction::kPowerwash));
  CF_EXPECT(WaitForRead(monitor_socket, FLAGS_wait_for_launcher));
  LauncherResponse powerwash_response =
      CF_EXPECT(ReadLauncherResponse(monitor_socket));
  CF_EXPECT(
      powerwash_response == LauncherResponse::kSuccess,
      "Received `" << static_cast<char>(powerwash_response)
                   << "` response from launcher monitor for powerwash request");

  LOG(INFO) << "Waiting for device to boot up again";
  CF_EXPECT(WaitForRead(monitor_socket, FLAGS_boot_timeout));
  RunnerExitCodes boot_exit_code = CF_EXPECT(ReadExitCode(monitor_socket));
  CF_EXPECT(boot_exit_code != RunnerExitCodes::kVirtualDeviceBootFailed,
            "Boot failed");
  CF_EXPECT(boot_exit_code == RunnerExitCodes::kSuccess,
            "Unknown response" << static_cast<int>(boot_exit_code));

  LOG(INFO) << "Powerwash successful";
  return {};
}

} // namespace
} // namespace cuttlefish

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  cuttlefish::Result<void> result = cuttlefish::PowerwashCvdMain();
  if (!result.ok()) {
    LOG(ERROR) << result.error().Message();
    LOG(DEBUG) << result.error().Trace();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
