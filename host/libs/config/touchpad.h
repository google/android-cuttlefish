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

#include <optional>
#include <string>

#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

constexpr const char kTouchpadFlag[] = "touchpad";
constexpr const char kTouchpadHelp[] =
    "Comma separated key=value pairs of touchpad properties. Supported "
    "properties:\n"
    " 'width': required, width of the touchpad in pixels\n"
    " 'height': required, height of the touchpad in pixels\n"
    ". Example usage: \n"
    "--touchpad=width=640,height=480\n";

Result<CuttlefishConfig::TouchpadConfig> ParseTouchpadConfig(
    const std::string& flag);

Result<std::vector<CuttlefishConfig::TouchpadConfig>>
ParseTouchpadConfigsFromArgs(std::vector<std::string>& args);

}  // namespace cuttlefish