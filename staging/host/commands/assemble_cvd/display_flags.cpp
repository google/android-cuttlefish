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

#include "host/commands/assemble_cvd/display_flags.h"

#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "host/commands/assemble_cvd/flags_defaults.h"

namespace cuttlefish {

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

}  // namespace cuttlefish