/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/flags/blank_data_image_mb.h"

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"

DEFINE_string(blank_data_image_mb, CF_DEFAULTS_BLANK_DATA_IMAGE_MB,
              "The size of the blank data image to generate, MB.");

namespace cuttlefish {
namespace {

constexpr char kFlagName[] = "blank_data_image_mb";

Result<int> GetDefaultValue(
    const std::map<std::string, std::string>& default_value_lookup) {
  auto lookup = default_value_lookup.find(kFlagName);
  CF_EXPECT(lookup != default_value_lookup.end());
  int result;
  CF_EXPECTF(android::base::ParseInt(lookup->second, &result),
             "Failed to parse value as integer: \"{}\"", lookup->second);
  return result;
}

}  // namespace

Result<BlankDataImageMbFlag> BlankDataImageMbFlag::FromGlobalGflags(
    const std::map<std::string, std::string>& default_value_lookup,
    const std::vector<GuestConfig>& guest_configs) {
  const int default_value = CF_EXPECT(GetDefaultValue(default_value_lookup));

  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie(kFlagName);
  std::vector<std::string> string_values =
      android::base::Split(flag_info.current_value, ",");
  std::vector<int> values(string_values.size());

  for (int i = 0; i < string_values.size(); i++) {
    if (string_values[i] != "unset" && string_values[i] != "\"unset\"") {
      int result;
      CF_EXPECTF(android::base::ParseInt(string_values[i], &result),
                 "Failed to parse value as integer: \"{}\"", string_values[i]);
      values[i] = result;
    } else {
      values[i] = default_value;
    }
  }

  return BlankDataImageMbFlag(default_value, guest_configs, std::move(values));
}

int BlankDataImageMbFlag::ForIndex(std::size_t index) const {
  if (index < blank_data_image_mb_values_.size()) {
    return blank_data_image_mb_values_[index];
  } else if (index < guest_configs_.size() &&
             guest_configs_[index].blank_data_image_mb != 0) {
    return guest_configs_[index].blank_data_image_mb;
  } else {
    return default_value_;
  }
}

BlankDataImageMbFlag::BlankDataImageMbFlag(
    const int default_value, const std::vector<GuestConfig>& guest_configs,
    std::vector<int> blank_data_image_mb_values)
    : default_value_(default_value),
      guest_configs_(guest_configs),
      blank_data_image_mb_values_(blank_data_image_mb_values) {}

}  // namespace cuttlefish
