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

#include "cuttlefish/host/commands/cvd/cli/parser/instance/cf_media_configs.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <string>
#include <vector>

#include "cuttlefish/host/commands/cvd/cli/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

using cvd::config::EnvironmentSpecification;
using cvd::config::Instance;
using cvd::config::MediaDevice;
using cvd::config::V4l2StreamProxy;

std::string MediaDeviceToFlagValue(const MediaDevice& device) {
  std::string res;
  if (device.has_v4l2_emulated_camera_splane()) {
    res = "v4l2_emulated_camera_splane";
  } else if (device.has_v4l2_emulated_camera_mplane()) {
    res = "v4l2_emulated_camera_mplane";
  } else if (device.has_v4l2_proxy()) {
    // TODO(b/520114678): Use device.v4l2_proxy.device_path when supported.
    res = "v4l2_proxy";
  } else if (device.has_v4l2_stream_proxy()) {
    const V4l2StreamProxy& v4l2_stream_proxy = device.v4l2_stream_proxy();
    res = "v4l2_stream_proxy";
    res += ":input_path=" + v4l2_stream_proxy.input_path();
    res += ":input_width=" + std::to_string(v4l2_stream_proxy.input_width());
    res += ":input_height=" + std::to_string(v4l2_stream_proxy.input_height());
    res += ":input_fps=" + v4l2_stream_proxy.input_fps();
  }
  if (device.has_lens_facing()) {
    res += ":lens_facing=" + device.lens_facing();
  }
  return res;
}

}  // namespace

Result<std::vector<std::string>> GenerateMediaFlags(
    const EnvironmentSpecification& cfg) {
  if (cfg.instances().empty()) {
    return {};
  }

  // Maximum number of media devices among all the instances which determines
  // the number of "--media" flags that would be used in downstream cvd CLI.
  const int max_devs = std::accumulate(
      cfg.instances().begin(), cfg.instances().end(), 0,
      [](int current_max, const Instance& ins) {
        return std::max(current_max, ins.media().devices().size());
      });
  if (max_devs == 0) {
    return {};
  }

  std::vector<std::string> flags;
  for (int idx = 0; idx < max_devs; idx++) {
    std::vector<std::string> values;
    for (const Instance& ins : cfg.instances()) {
      if (idx < ins.media().devices().size()) {
        values.push_back(MediaDeviceToFlagValue(ins.media().devices()[idx]));
      } else {
        values.push_back("");
      }
    }
    flags.push_back(GenerateVecFlag("media", values));
  }

  return flags;
}

}  // namespace cuttlefish
