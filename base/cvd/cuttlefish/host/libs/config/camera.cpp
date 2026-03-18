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

#include "cuttlefish/host/libs/config/camera.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

static constexpr char kCameraTypeV4l2Emulated[] = "v4l2_emulated";
static constexpr char kCameraTypeV4l2Proxy[] = "v4l2_proxy";

Result<std::optional<CuttlefishConfig::CameraConfig>> ParseCameraConfig(
    const std::string& flag) {
  std::unordered_map<std::string, std::string> props;
  if (!flag.empty()) {
    const std::vector<std::string> pairs = android::base::Split(flag, ",");
    for (const std::string& pair : pairs) {
      const std::vector<std::string> keyvalue = android::base::Split(pair, "=");
      CF_EXPECT_EQ(keyvalue.size(), 2,
                   "Invalid camera flag key-value: \"" << flag << "\"");
      const std::string& prop_key = keyvalue[0];
      const std::string& prop_val = keyvalue[1];
      props[prop_key] = prop_val;
    }
  }

  auto type_it = props.find("type");
  CF_EXPECT(type_it != props.end(), "Missing camera type");
  CuttlefishConfig::CameraType type { CuttlefishConfig::CameraType::kUnknown };
  if (type_it->second == kCameraTypeV4l2Emulated) {
    type = CuttlefishConfig::CameraType::kV4l2Emulated;
  } else if (type_it->second == kCameraTypeV4l2Proxy) {
    type = CuttlefishConfig::CameraType::kV4l2Proxy;
  } else {
    return CF_ERRF("Unknown camera type value: \"{}\"", type_it->second);
  }

  return CuttlefishConfig::CameraConfig{
      .type = type,
  };
}

Result<std::vector<CuttlefishConfig::CameraConfig>> ParseCameraConfigsFromArgs(
    std::vector<std::string>& args) {
  std::vector<std::string> repeated_camera_flag_values;
  const std::vector<Flag> camera_flags = {
      GflagsCompatFlag(kCameraFlag)
          .Help(kCameraHelp)
          .Setter([&](const FlagMatch& match) -> Result<void> {
            repeated_camera_flag_values.push_back(match.value);
            return {};
          }),
  };
  CF_EXPECT(ConsumeFlags(camera_flags, args), "Failed to parse camera flags.");
  std::vector<CuttlefishConfig::CameraConfig> configs;
  for (const std::string& param : repeated_camera_flag_values) {
    auto config = CF_EXPECT(ParseCameraConfig(param));
    if (config) {
      configs.push_back(*config);
    }
  }
  return configs;
}

}  // namespace cuttlefish

