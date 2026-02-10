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

#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

// identity function, to match other type parsers
Result<std::string> ParseString(const std::string& value, const std::string&) {
  return value;
}

template <typename T>
Result<FromGflags<T>> FromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info, const std::string& flag_name,
    std::vector<T> default_values,
    std::function<Result<T>(const std::string& value, const std::string& name)>
        parse_func) {
  CF_EXPECT(!default_values.empty());

  std::vector<std::string> string_values =
      android::base::Split(flag_info.current_value, ",");
  std::vector<T> values(string_values.size());

  for (int i = 0; i < string_values.size(); i++) {
    if (string_values[i] == "unset" || string_values[i] == "\"unset\"") {
      values[i] = default_values[i < default_values.size() ? i : 0];
    } else {
      values[i] = CF_EXPECT(parse_func(string_values[i], flag_name));
    }
  }
  return FromGflags<T>{
      .values = std::move(values),
      // TODO: chadreynolds - replace with
      // gflags::WasPresentOnCommandLine(flag_name) if it becomes available
      // That function is more accurate at determining if the flag was
      // user-provided
      .is_default = flag_info.is_default,
  };
}

template <typename T>
Result<FromGflags<T>> FromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info, const std::string& flag_name,
    std::function<Result<T>(const std::string& value, const std::string& name)>
        parse_func) {
  std::vector<std::string> default_string_values =
      android::base::Split(flag_info.default_value, ",");
  std::vector<T> default_values;
  default_values.reserve(default_string_values.size());
  for (const std::string& default_string_value : default_string_values) {
    default_values.emplace_back(
        CF_EXPECT(parse_func(default_string_value, flag_name)));
  }
  return std::move(
      FromGlobalGflags<T>(flag_info, flag_name, default_values, parse_func));
}

}  // namespace

template struct FromGflags<bool>;
template struct FromGflags<int>;
template struct FromGflags<std::string>;

Result<FromGflags<bool>> BoolFromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info,
    const std::string& flag_name) {
  return CF_EXPECT(FromGlobalGflags<bool>(flag_info, flag_name, ParseBool));
}

Result<FromGflags<bool>> BoolFromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info, const std::string& flag_name,
    bool default_value) {
  return CF_EXPECT(
      FromGlobalGflags<bool>(flag_info, flag_name, {default_value}, ParseBool));
}

Result<FromGflags<int>> IntFromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info,
    const std::string& flag_name) {
  return CF_EXPECT(FromGlobalGflags<int>(flag_info, flag_name, ParseInt));
}

Result<FromGflags<std::string>> StringFromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info,
    const std::string& flag_name) {
  return CF_EXPECT(
      FromGlobalGflags<std::string>(flag_info, flag_name, ParseString));
}

}  // namespace cuttlefish
