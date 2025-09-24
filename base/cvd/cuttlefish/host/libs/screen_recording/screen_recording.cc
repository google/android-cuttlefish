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

#include "cuttlefish/host/libs/screen_recording/screen_recording.h"

#include <chrono>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/command_util/runner/run_cvd.pb.h"
#include "cuttlefish/host/libs/command_util/util.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

Result<void> StartScreenRecording(
    const CuttlefishConfig::InstanceSpecific& instance_config,
    std::chrono::seconds wait_for_launcher) {
  SharedFD launcher_monitor = CF_EXPECT(GetLauncherMonitorFromInstance(
      instance_config, wait_for_launcher.count()));

  run_cvd::ExtendedLauncherAction extended_action;
  extended_action.mutable_start_screen_recording();
  CF_EXPECT(RunLauncherAction(launcher_monitor, extended_action, std::nullopt));
  return {};
}

Result<void> StopScreenRecording(
    const CuttlefishConfig::InstanceSpecific& instance_config,
    std::chrono::seconds wait_for_launcher) {
  SharedFD launcher_monitor = CF_EXPECT(GetLauncherMonitorFromInstance(
      instance_config, wait_for_launcher.count()));

  run_cvd::ExtendedLauncherAction extended_action;
  extended_action.mutable_stop_screen_recording();
  CF_EXPECT(RunLauncherAction(launcher_monitor, extended_action, std::nullopt));
  return {};
}

}  // namespace cuttlefish

