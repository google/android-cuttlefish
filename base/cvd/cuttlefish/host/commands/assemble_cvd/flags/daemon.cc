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

#include "cuttlefish/host/commands/assemble_cvd/flags/daemon.h"

#include <cstdlib>
#include <string>
#include <vector>

#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(daemon, CF_DEFAULTS_DAEMON ? "true" : "false",
              "Run cuttlefish in background, the launcher exits on boot "
              "completed/failed");

namespace cuttlefish {

Result<DaemonFlag> DaemonFlag::FromGlobalGflags() {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("daemon");
  bool default_value = CF_EXPECT(ParseBool(flag_info.default_value, "daemon"));

  std::vector<std::string> string_values =
      android::base::Split(flag_info.current_value, ",");
  std::vector<bool> values(string_values.size());

  for (int i = 0; i < string_values.size(); i++) {
    if (string_values[i] == "unset" || string_values[i] == "\"unset\"") {
      values[i] = default_value;
    } else {
      values[i] = CF_EXPECT(ParseBool(string_values[i], "daemon"));
    }
  }
  return DaemonFlag(default_value, std::move(values));
}

bool DaemonFlag::ForIndex(std::size_t index) const {
  if (index < daemon_values_.size()) {
    return daemon_values_[index];
  } else {
    return default_value_;
  }
}

DaemonFlag::DaemonFlag(const bool default_value,
                       std::vector<bool> daemon_values)
    : default_value_(default_value), daemon_values_(daemon_values) {}

}  // namespace cuttlefish
