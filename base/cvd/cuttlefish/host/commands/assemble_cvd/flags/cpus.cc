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

#include "cuttlefish/host/commands/assemble_cvd/flags/cpus.h"

#include <cstdlib>
#include <string>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(cpus, std::to_string(CF_DEFAULTS_CPUS), "Virtual CPU count.");

namespace cuttlefish {

Result<CpusFlag> CpusFlag::FromGlobalGflags() {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("cpus");
  int default_value;
  CF_EXPECTF(android::base::ParseInt(flag_info.default_value, &default_value),
             "Failed to parse value as integer: \"{}\"",
             flag_info.default_value);

  std::vector<std::string> string_values =
      android::base::Split(flag_info.current_value, ",");
  std::vector<int> values(string_values.size());

  for (int i = 0; i < string_values.size(); i++) {
    if (string_values[i] == "unset" || string_values[i] == "\"unset\"") {
      values[i] = default_value;
    } else {
      CF_EXPECTF(android::base::ParseInt(string_values[i], &values[i]),
                 "Failed to parse value as integer: \"{}\"", string_values[i]);
    }
  }
  return CpusFlag(default_value, std::move(values));
}

int CpusFlag::ForIndex(std::size_t index) const {
  if (index < cpus_values_.size()) {
    return cpus_values_[index];
  } else {
    return default_value_;
  }
}

CpusFlag::CpusFlag(const int default_value, std::vector<int> cpus_values)
    : default_value_(default_value), cpus_values_(cpus_values) {}

}  // namespace cuttlefish
