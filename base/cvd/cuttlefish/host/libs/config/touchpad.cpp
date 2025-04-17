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

#include "host/libs/config/touchpad.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "host/commands/assemble_cvd/flags_defaults.h"

namespace cuttlefish {

Result<CuttlefishConfig::TouchpadConfig> ParseTouchpadConfig(
    const std::string& flag) {
  CF_EXPECT(!flag.empty(), "Touchpad configuration empty");

  std::unordered_map<std::string, std::string> props;
  for (const auto& pair : android::base::Split(flag, ",")) {
    const std::vector<std::string> keyvalue = android::base::Split(pair, "=");
    CF_EXPECT_EQ(keyvalue.size(), 2,
                 "Invalid touchpad flag key-value: \"" << flag << "\"");
    const std::string& prop_key = keyvalue[0];
    const std::string& prop_val = keyvalue[1];
    props[prop_key] = prop_val;
  }

  CF_EXPECT(Contains(props, "width"),
            "Touchpad configuration missing 'width' in \"" << flag << "\"");
  CF_EXPECT(Contains(props, "height"),
            "Touchpad configuration missing 'height' in \"" << flag << "\"");
  CF_EXPECT_EQ(
      props.size(), 2,
      "Touchpad configuration should only have width and height properties");

  CuttlefishConfig::TouchpadConfig config = {};
  CF_EXPECT(android::base::ParseInt(props["width"], &config.width),
            "Touchpad configuration invalid 'width' in \"" << flag << "\"");
  CF_EXPECT(android::base::ParseInt(props["height"], &config.height),
            "Touchpad configuration invalid 'height' in \"" << flag << "\"");

  return config;
}

Result<std::vector<CuttlefishConfig::TouchpadConfig>>
ParseTouchpadConfigsFromArgs(std::vector<std::string>& args) {
  std::vector<std::string> repeated_touchpad_flag_values;

  const std::vector<Flag> touchpad_flags = {
      GflagsCompatFlag(kTouchpadFlag)
          .Help(kTouchpadHelp)
          .Setter([&](const FlagMatch& match) -> Result<void> {
            repeated_touchpad_flag_values.push_back(match.value);
            return {};
          }),
  };

  CF_EXPECT(ConsumeFlags(touchpad_flags, args),
            "Failed to parse touchpad flags.");

  std::vector<CuttlefishConfig::TouchpadConfig> touchpad_configs;

  for (const std::string& touchpad_params : repeated_touchpad_flag_values) {
    auto touchpad = CF_EXPECT(ParseTouchpadConfig(touchpad_params));
    touchpad_configs.push_back(touchpad);
  }

  return touchpad_configs;
}

}  // namespace cuttlefish
