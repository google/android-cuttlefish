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

#include "host/libs/config/display.h"

#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "host/commands/assemble_cvd/flags_defaults.h"

namespace cuttlefish {
namespace {

static constexpr char kDisplay0FlagName[] = "display0";
static constexpr char kDisplay1FlagName[] = "display1";
static constexpr char kDisplay2FlagName[] = "display2";
static constexpr char kDisplay3FlagName[] = "display3";

}  // namespace

Result<std::optional<CuttlefishConfig::DisplayConfig>> ParseDisplayConfig(
    const std::string& flag) {
  if (flag.empty()) {
    return std::nullopt;
  }

  std::unordered_map<std::string, std::string> props;

  const std::vector<std::string> pairs = android::base::Split(flag, ",");
  for (const std::string& pair : pairs) {
    const std::vector<std::string> keyvalue = android::base::Split(pair, "=");
    CF_EXPECT_EQ(keyvalue.size(), 2,
                 "Invalid display flag key-value: \"" << flag << "\"");
    const std::string& prop_key = keyvalue[0];
    const std::string& prop_val = keyvalue[1];
    props[prop_key] = prop_val;
  }

  CF_EXPECT(Contains(props, "width"),
            "Display configuration missing 'width' in \"" << flag << "\"");
  CF_EXPECT(Contains(props, "height"),
            "Display configuration missing 'height' in \"" << flag << "\"");

  int display_width;
  CF_EXPECT(android::base::ParseInt(props["width"], &display_width),
            "Display configuration invalid 'width' in \"" << flag << "\"");

  int display_height;
  CF_EXPECT(android::base::ParseInt(props["height"], &display_height),
            "Display configuration invalid 'height' in \"" << flag << "\"");

  int display_dpi = CF_DEFAULTS_DISPLAY_DPI;
  auto display_dpi_it = props.find("dpi");
  if (display_dpi_it != props.end()) {
    CF_EXPECT(android::base::ParseInt(display_dpi_it->second, &display_dpi),
              "Display configuration invalid 'dpi' in \"" << flag << "\"");
  }

  int display_refresh_rate_hz = CF_DEFAULTS_DISPLAY_REFRESH_RATE;
  auto display_refresh_rate_hz_it = props.find("refresh_rate_hz");
  if (display_refresh_rate_hz_it != props.end()) {
    CF_EXPECT(android::base::ParseInt(display_refresh_rate_hz_it->second,
                                      &display_refresh_rate_hz),
              "Display configuration invalid 'refresh_rate_hz' in \"" << flag
                                                                      << "\"");
  }

  return CuttlefishConfig::DisplayConfig{
      .width = display_width,
      .height = display_height,
      .dpi = display_dpi,
      .refresh_rate_hz = display_refresh_rate_hz,
  };
}

Result<std::vector<CuttlefishConfig::DisplayConfig>>
ParseDisplayConfigsFromArgs(std::vector<std::string>& args) {
  std::string display0_flag_value;
  std::string display1_flag_value;
  std::string display2_flag_value;
  std::string display3_flag_value;
  std::vector<std::string> repeated_display_flag_values;

  const std::vector<Flag> display_flags = {
      GflagsCompatFlag(kDisplay0FlagName, display0_flag_value)
          .Help(kDisplayHelp),
      GflagsCompatFlag(kDisplay1FlagName, display1_flag_value)
          .Help(kDisplayHelp),
      GflagsCompatFlag(kDisplay2FlagName, display2_flag_value)
          .Help(kDisplayHelp),
      GflagsCompatFlag(kDisplay3FlagName, display3_flag_value)
          .Help(kDisplayHelp),
      GflagsCompatFlag(kDisplayFlag)
          .Help(kDisplayHelp)
          .Setter([&](const FlagMatch& match) {
            repeated_display_flag_values.push_back(match.value);
            return true;
          }),
  };

  CF_EXPECT(ParseFlags(display_flags, args), "Failed to parse display flags.");

  std::vector<CuttlefishConfig::DisplayConfig> displays_configs;

  auto display0 = CF_EXPECT(ParseDisplayConfig(display0_flag_value));
  if (display0) {
    displays_configs.push_back(*display0);
  }
  auto display1 = CF_EXPECT(ParseDisplayConfig(display1_flag_value));
  if (display1) {
    displays_configs.push_back(*display1);
  }
  auto display2 = CF_EXPECT(ParseDisplayConfig(display2_flag_value));
  if (display2) {
    displays_configs.push_back(*display2);
  }
  auto display3 = CF_EXPECT(ParseDisplayConfig(display3_flag_value));
  if (display3) {
    displays_configs.push_back(*display3);
  }

  for (const std::string& display_params : repeated_display_flag_values) {
    auto display = CF_EXPECT(ParseDisplayConfig(display_params));
    if (display) {
      displays_configs.push_back(*display);
    }
  }

  return displays_configs;
}

}  // namespace cuttlefish