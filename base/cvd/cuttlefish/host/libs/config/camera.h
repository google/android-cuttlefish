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

constexpr const char kCameraFlag[] = "camera";
constexpr const char kCameraHelp[] =
    "NOTE: Flag is only used to manage virtio-media host emulated cameras "
    "devices. It's **NOT** used or meant for managing Guest Emulated Cameras.\n"
    "\n"
    "Comma separated key=value pairs of camera properties. Supported "
    "properties:\n"
    " 'type': optional, default to 'v4l2_emulated', supported values:\n"
    "    'v4l2_emulated': host emulated camera streamed through v4l2\n"
    "    'v4l2_proxy': host external camera streamed through v4l2\n "
    "Example usage:\n"
    "  --camera=type=v4l2_emulated\n";

Result<std::optional<CuttlefishConfig::CameraConfig>> ParseCameraConfig(
    const std::string& flag);

Result<std::vector<CuttlefishConfig::CameraConfig>> ParseCameraConfigsFromArgs(
    std::vector<std::string>& args);

}  // namespace cuttlefish

