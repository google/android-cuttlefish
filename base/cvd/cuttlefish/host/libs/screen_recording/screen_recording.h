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

#include <chrono>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

Result<void> StartScreenRecording(
    const CuttlefishConfig::InstanceSpecific& instance_config,
    std::chrono::seconds wait_for_launcher);

Result<void> StopScreenRecording(
    const CuttlefishConfig::InstanceSpecific& instance_config,
    std::chrono::seconds wait_for_launcher);

}  // namespace cuttlefish
