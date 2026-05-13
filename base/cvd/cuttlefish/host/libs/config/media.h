/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <optional>
#include <string>

#include "cuttlefish/result/result.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

constexpr const char kMediaFlag[] = "media";
constexpr const char kMediaHelp[] =
    "Comma separated key=value pairs of media device properties. Supported "
    "properties:\n"
    " 'type': optional, defaults to 'v4l2_emulated_camera_splane', supported values:\n"
    "    'v4l2_emulated_camera_splane': emulated media capture device (single-plane)\n"
    "    'v4l2_emulated_camera_mplane': emulated media capture device (multi-plane)\n"
    "    'v4l2_proxy': proxy a host V4L2 device into the guest\n "
    "Example usage:\n"
    "  --media=type=v4l2_emulated_camera_splane\n";

Result<std::optional<CuttlefishConfig::MediaConfig>> ParseMediaConfig(
    const std::string& flag);

Result<std::vector<CuttlefishConfig::MediaConfig>> ParseMediaConfigsFromArgs(
    std::vector<std::string>& args);

}  // namespace cuttlefish

