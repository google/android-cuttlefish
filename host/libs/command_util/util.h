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
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

Result<LauncherResponse> ReadLauncherResponse(const SharedFD& monitor_socket);

Result<RunnerExitCodes> ReadExitCode(const SharedFD& monitor_socket);

Result<SharedFD> GetLauncherMonitorFromInstance(
    const CuttlefishConfig::InstanceSpecific& instance_config,
    const int timeout_seconds);

Result<SharedFD> GetLauncherMonitor(const CuttlefishConfig& config,
                                    const int instance_num,
                                    const int timeout_seconds);

Result<void> WriteLauncherAction(const SharedFD& monitor_socket,
                                 const LauncherAction request);

Result<void> WaitForRead(const SharedFD& monitor_socket,
                         const int timeout_seconds);

}  // namespace cuttlefish
