/*
 * Copyright (C) 2022 The Android Open Source Project
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

#pragma once

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "device/google/cuttlefish/host/libs/command_util/runner/run_cvd.pb.h"
#include "host/libs/command_util/runner/defs.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

Result<RunnerExitCodes> ReadExitCode(const SharedFD& monitor_socket);

Result<SharedFD> GetLauncherMonitorFromInstance(
    const CuttlefishConfig::InstanceSpecific& instance_config,
    const int timeout_seconds);

Result<SharedFD> GetLauncherMonitor(const CuttlefishConfig& config,
                                    const int instance_num,
                                    const int timeout_seconds);

struct LauncherActionInfo {
  LauncherAction action;
  run_cvd::ExtendedLauncherAction extended_action;
};
Result<LauncherActionInfo> ReadLauncherActionFromFd(
    const SharedFD& monitor_socket);

Result<void> WaitForRead(const SharedFD& monitor_socket,
                         const int timeout_seconds);

// Writes the `action` request to `monitor_socket`, then waits for the response
// and checks for errors.
Result<void> RunLauncherAction(const SharedFD& monitor_socket,
                               LauncherAction action,
                               std::optional<int> timeout_seconds);

// Writes the `action` request to `monitor_socket`, then waits for the response
// and checks for errors.
Result<void> RunLauncherAction(
    const SharedFD& monitor_socket, ExtendedActionType extended_action_type,
    const run_cvd::ExtendedLauncherAction& extended_action,
    std::optional<int> timeout_seconds);

}  // namespace cuttlefish
