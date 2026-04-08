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

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

static constexpr char kMediaTypeV4l2EmulatedCamera[] = "v4l2_emulated_camera";
static constexpr char kMediaTypeV4l2Proxy[] = "v4l2_proxy";

Result<std::optional<CuttlefishConfig::MediaConfig>> ParseMediaConfig(
    const std::string& flag) {
  std::unordered_map<std::string, std::string> props;
  if (!flag.empty()) {
    const std::vector<std::string> pairs = absl::StrSplit(flag, ",");
    for (const std::string& pair : pairs) {
      const std::vector<std::string> keyvalue = absl::StrSplit(pair, "=");
      CF_EXPECT_EQ(keyvalue.size(), 2,
                   "Invalid media flag key-value: \"" << flag << "\"");
      const std::string& prop_key = keyvalue[0];
      const std::string& prop_val = keyvalue[1];
      props[prop_key] = prop_val;
    }
  }

  auto type_it = props.find("type");
  CF_EXPECT(type_it != props.end(), "Missing media type");
  CuttlefishConfig::MediaType type { CuttlefishConfig::MediaType::kUnknown };
  if (type_it->second == kMediaTypeV4l2EmulatedCamera) {
    type = CuttlefishConfig::MediaType::kV4l2EmulatedCamera;
  } else if (type_it->second == kMediaTypeV4l2Proxy) {
    type = CuttlefishConfig::MediaType::kV4l2Proxy;
  } else {
    return CF_ERRF("Unknown media type value: \"{}\"", type_it->second);
  }

  return CuttlefishConfig::MediaConfig{
      .type = type,
  };
}

Result<std::vector<CuttlefishConfig::MediaConfig>> ParseMediaConfigsFromArgs(
    std::vector<std::string>& args) {
  std::vector<std::string> repeated_media_flag_values;
  const std::vector<Flag> media_flags = {
      GflagsCompatFlag(kMediaFlag)
          .Help(kMediaHelp)
          .Setter([&](const FlagMatch& match) -> Result<void> {
            repeated_media_flag_values.push_back(match.value);
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

