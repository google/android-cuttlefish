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

#include "cuttlefish/host/commands/assemble_cvd/flags/bool_flag.h"

#include <cstdlib>
#include <string>
#include <vector>

#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

Result<BoolFlag> BoolFlag::FromGlobalGflagsAndName(
    const std::string& flag_name) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie(flag_name.c_str());
  bool default_value = CF_EXPECT(ParseBool(flag_info.default_value, flag_name));

  std::vector<std::string> string_values =
      android::base::Split(flag_info.current_value, ",");
  std::vector<bool> values(string_values.size());

  for (int i = 0; i < string_values.size(); i++) {
    if (string_values[i] == "unset" || string_values[i] == "\"unset\"") {
      values[i] = default_value;
    } else {
      values[i] = CF_EXPECT(ParseBool(string_values[i], flag_name));
    }
  }
  return BoolFlag(default_value, std::move(values));
}

bool BoolFlag::ForIndex(const std::size_t index) const {
  if (index < values_.size()) {
    return values_[index];
  } else {
    return default_value_;
  }
}

BoolFlag::BoolFlag(const bool default_value, std::vector<bool> values)
    : default_value_(default_value), values_(values) {}

}  // namespace cuttlefish
