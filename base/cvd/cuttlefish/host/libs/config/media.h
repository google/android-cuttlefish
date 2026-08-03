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

#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

constexpr const char kMediaFlag[] = "media";
constexpr const char kMediaHelp[] =
    "Colon separated media device properties: "
    "\"[type]:[key1]=[val1]:[key2]=[val2]\". "
    "Supported types:\n"
    "    'v4l2_emulated_camera_splane': emulated media capture device "
    "(single-plane)\n"
    "    'v4l2_emulated_camera_mplane': emulated media capture device "
    "(multi-plane)\n"
    "    'v4l2_proxy': proxy a host V4L2 device into the guest\n"
    "    'v4l2_stream_proxy': stream video from a host named pipe into the "
    "guest\n\n"
    "v4l2_stream_proxy properties:\n"
    "    'input_path': path to the host named pipe\n"
    "    'input_width': width of the video stream in pixels\n"
    "    'input_height': height of the video stream in pixels\n"
    "    'input_fps': frames per second (e.g., 30 or 30000/1001)\n\n"
    "Supported keys:\n"
    "    'lens_facing': optional, supported values: 'FRONT', 'BACK', "
    "'EXTERNAL'\n\n"
    "Example usage:\n"
    "  --media=v4l2_emulated_camera_mplane:lens_facing=BACK\n"
    "  --media=v4l2_stream_proxy:input_path=/tmp/fifo:"
    "input_width=640:input_height=480:input_fps=30\n";

Result<std::optional<CuttlefishConfig::MediaConfig>> ParseMediaConfig(
    const std::string& flag);

Result<std::vector<CuttlefishConfig::MediaConfig>> ParseMediaConfigsFromArgs(
    std::vector<std::string>& args);

}  // namespace cuttlefish
