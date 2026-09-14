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

#include "cuttlefish/host/libs/config/media.h"

#include <android-base/parseint.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "absl/strings/str_split.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

static constexpr char kMediaTypeV4l2EmulatedCameraSPlane[] =
    "v4l2_emulated_camera_splane";
static constexpr char kMediaTypeV4l2EmulatedCameraMPlane[] =
    "v4l2_emulated_camera_mplane";
static constexpr char kMediaTypeV4l2Proxy[] = "v4l2_proxy";
static constexpr char kMediaTypeV4l2Stream[] = "v4l2_stream_proxy";

Result<std::optional<CuttlefishConfig::MediaConfig>> ParseMediaConfig(
    const std::string& flag) {
  const std::vector<std::string> parts = absl::StrSplit(flag, ":");
  CF_EXPECT(!parts.empty(), "Invalid media flag: \"" << flag << "\"");

  const std::string& type_str = parts[0];
  CuttlefishConfig::MediaType type{CuttlefishConfig::MediaType::kUnknown};
  if (type_str == kMediaTypeV4l2EmulatedCameraSPlane) {
    type = CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane;
  } else if (type_str == kMediaTypeV4l2EmulatedCameraMPlane) {
    type = CuttlefishConfig::MediaType::kV4l2EmulatedCameraMPlane;
  } else if (type_str == kMediaTypeV4l2Proxy) {
    type = CuttlefishConfig::MediaType::kV4l2Proxy;
  } else if (type_str == kMediaTypeV4l2Stream) {
    type = CuttlefishConfig::MediaType::kV4l2StreamProxy;
  } else {
    return CF_ERRF("Unknown media type value: \"{}\"", type_str);
  }

  std::unordered_map<std::string, std::string> props;
  for (size_t i = 1; i < parts.size(); ++i) {
    const std::vector<std::string> keyvalue = absl::StrSplit(parts[i], "=");
    CF_EXPECT_EQ(keyvalue.size(), 2,
                 "Invalid media flag key-value: \"" << parts[i] << "\" in \""
                                                    << flag << "\"");
    const std::string& prop_key = keyvalue[0];
    const std::string& prop_val = keyvalue[1];
    props[prop_key] = prop_val;
  }

  std::string lens_facing = "";
  auto lens_facing_it = props.find("lens_facing");
  if (lens_facing_it != props.end()) {
    lens_facing = lens_facing_it->second;
    CF_EXPECT(lens_facing == "FRONT" || lens_facing == "BACK" ||
                  lens_facing == "EXTERNAL",
              "Invalid lens_facing value: " << lens_facing);
  }

  std::optional<int> instance_index;
  auto instance_it = props.find("instance");
  if (instance_it != props.end()) {
    int idx = 0;
    CF_EXPECT(android::base::ParseInt(instance_it->second, &idx),
              "Failed to parse instance index: " << instance_it->second);
    CF_EXPECT(idx >= 0, "instance index must be non-negative: " << idx);
    instance_index = idx;
  }

  std::optional<CuttlefishConfig::MediaConfig::V4l2StreamProxyConfig>
      v4l2_stream_proxy;
  if (type == CuttlefishConfig::MediaType::kV4l2StreamProxy) {
    CuttlefishConfig::MediaConfig::V4l2StreamProxyConfig stream_config = {};

    auto source_it = props.find("input_path");
    CF_EXPECT(source_it != props.end(),
              "Missing 'input_path' for v4l2_stream_proxy");
    CF_EXPECT(!source_it->second.empty(),
              "'input_path' must not be empty for v4l2_stream_proxy");
    stream_config.input_path = source_it->second;

    auto width_it = props.find("input_width");
    CF_EXPECT(width_it != props.end(),
              "Missing 'input_width' for v4l2_stream_proxy");
    CF_EXPECT(
        android::base::ParseInt(width_it->second, &stream_config.input_width),
        "Failed to parse input_width");
    CF_EXPECT(stream_config.input_width > 0,
              "input_width must be positive: " << stream_config.input_width);

    auto height_it = props.find("input_height");
    CF_EXPECT(height_it != props.end(),
              "Missing 'input_height' for v4l2_stream_proxy");
    CF_EXPECT(
        android::base::ParseInt(height_it->second, &stream_config.input_height),
        "Failed to parse input_height");
    CF_EXPECT(stream_config.input_height > 0,
              "input_height must be positive: " << stream_config.input_height);

    auto fps_it = props.find("input_fps");
    CF_EXPECT(fps_it != props.end(),
              "Missing 'input_fps' for v4l2_stream_proxy");
    CF_EXPECT(!fps_it->second.empty(),
              "'input_fps' must not be empty for v4l2_stream_proxy");
    stream_config.input_fps = fps_it->second;

    v4l2_stream_proxy = stream_config;
  }

  return CuttlefishConfig::MediaConfig{
      .type = type,
      .lens_facing = lens_facing,
      .instance_index = instance_index,
      .v4l2_stream_proxy = v4l2_stream_proxy,
  };
}

Result<std::vector<CuttlefishConfig::MediaConfig>> ParseMediaConfigsFromArgs(
    std::vector<std::string>& args) {
  std::vector<std::string> repeated_media_flag_values;
  const std::vector<Flag> media_flags = {
      Flag::StringFlag(kMediaFlag)
          .Help(kMediaHelp)
          .Setter([&](std::string_view arg) -> Result<void> {
            repeated_media_flag_values.emplace_back(arg);
            return {};
          }),
  };
  CF_EXPECT(ConsumeFlags(media_flags, args), "Failed to parse media flags.");
  std::vector<CuttlefishConfig::MediaConfig> configs;
  for (const std::string& param : repeated_media_flag_values) {
    auto config = CF_EXPECT(ParseMediaConfig(param));
    if (config) {
      configs.push_back(*config);
    }
  }
  return configs;
}

}  // namespace cuttlefish
