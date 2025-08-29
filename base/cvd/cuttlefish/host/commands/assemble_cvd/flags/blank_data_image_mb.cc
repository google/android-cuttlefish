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

Result<BlankDataImageMbFlag> BlankDataImageMbFlag::FromGlobalGflags(
    const std::vector<GuestConfig>& guest_configs) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("blank_data_image_mb");
  int default_value;
  CF_EXPECTF(android::base::ParseInt(flag_info.default_value, &default_value),
             "Failed to parse value as integer: \"{}\"",
             flag_info.default_value);

  std::vector<std::string> string_values =
      android::base::Split(flag_info.current_value, ",");
  const std::size_t size = guest_configs.size() > string_values.size()
                               ? guest_configs.size()
                               : string_values.size();
  std::vector<int> values(size);

  for (int i = 0; i < size; i++) {
    if (i < string_values.size()) {
      if (string_values[i] == "unset" || string_values[i] == "\"unset\"") {
        values[i] = default_value;
      } else {
        CF_EXPECTF(android::base::ParseInt(string_values[i], &values[i]),
                   "Failed to parse value as integer: \"{}\"",
                   string_values[i]);
      }
    } else {
      values[i] = guest_configs[i].blank_data_image_mb;
    }
  }
  return BlankDataImageMbFlag(default_value, std::move(values));
}

int BlankDataImageMbFlag::ForIndex(std::size_t index) const {
  if (index < blank_data_image_mb_values_.size()) {
    return blank_data_image_mb_values_[index];
  } else {
    return default_value_;
  }
}

BlankDataImageMbFlag::BlankDataImageMbFlag(
    const int default_value, std::vector<int> blank_data_image_mb_values)
    : default_value_(default_value),
      blank_data_image_mb_values_(blank_data_image_mb_values) {}

}  // namespace cuttlefish
