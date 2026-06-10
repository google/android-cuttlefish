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

#include <string>
#include <vector>

#include "cuttlefish/host/commands/cvd/cli/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

using cvd::config::EnvironmentSpecification;
using cvd::config::Instance;

Result<std::vector<std::string>> GenerateMediaFlags(
    const EnvironmentSpecification& cfg) {
  int has_media_count =
      std::count_if(cfg.instances().rbegin(), cfg.instances().rend(),
                    [](const auto& v) { return v.has_media(); });
  CF_EXPECT(has_media_count == 0 || cfg.instances().size() == 1,
            "TODO(b/520098369): support media devices for multiple instances");
  std::vector<std::string> flags;
  for (const auto& instance : cfg.instances()) {
    if (instance.has_media()) {
      for (const auto& device : instance.media().devices()) {
        std::string flag = "--media=";
        if (device.has_v4l2_emulated_camera_splane()) {
          flag += "type=v4l2_emulated_camera_splane";
        } else if (device.has_v4l2_emulated_camera_mplane()) {
          flag += "type=v4l2_emulated_camera_mplane";
        } else if (device.has_v4l2_proxy()) {
          // TODO(b/520114678): Use device.v4l2_proxy.device_path when
          // supported.
          flag += "type=v4l2_proxy";
        }
        if (device.has_lens_facing()) {
          flag += ",lens_facing=" + device.lens_facing();
        }
        flags.push_back(flag);
      }
    }
  }
  return flags;
}

}  // namespace cuttlefish
