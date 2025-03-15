//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "host/commands/start/override_bool_arg.h"

#include <string>
#include <unordered_set>
#include <vector>

#include <android-base/strings.h>

namespace cuttlefish {
namespace {

struct BooleanFlag {
  bool is_bool_flag;
  bool bool_flag_value;
  std::string name;
};
BooleanFlag IsBoolArg(const std::string& argument,
                      const std::unordered_set<std::string>& flag_set) {
  // Validate format
  // we only deal with special bool case: -flag, --flag, -noflag, --noflag
  // and convert to -flag=true, --flag=true, -flag=false, --flag=false
  // others not in this format just return false
  std::string_view name = argument;
  if (!android::base::ConsumePrefix(&name, "-")) {
    return {false, false, ""};
  }
  android::base::ConsumePrefix(&name, "-");
  std::size_t found = name.find('=');
  if (found != std::string::npos) {
    // found "=", --flag=value case, it doesn't need convert
    return {false, false, ""};
  }

  // Validate it is part of the set
  std::string result_name(name);
  std::string_view new_name = result_name;
  if (result_name.length() == 0) {
    return {false, false, ""};
  }
  if (flag_set.find(result_name) != flag_set.end()) {
    // matched -flag, --flag
    return {true, true, result_name};
  } else if (android::base::ConsumePrefix(&new_name, "no")) {
    // 2nd chance to check -noflag, --noflag
    result_name = new_name;
    if (flag_set.find(result_name) != flag_set.end()) {
      // matched -noflag, --noflag
      return {true, false, result_name};
    }
  }
  // return status
  return {false, false, ""};
}

std::string FormatBoolString(const std::string& name_str, bool value) {
  std::string new_flag = "--" + name_str;
  if (value) {
    new_flag += "=true";
  } else {
    new_flag += "=false";
  }
  return new_flag;
}

}  // namespace

std::vector<std::string> OverrideBoolArg(
    std::vector<std::string> args,
    const std::unordered_set<std::string>& flag_set) {
  for (int index = 0; index < args.size(); index++) {
    const std::string curr_arg = args[index];
    BooleanFlag value = IsBoolArg(curr_arg, flag_set);
    if (value.is_bool_flag) {
      // Override the value
      args[index] = FormatBoolString(value.name, value.bool_flag_value);
    }
  }
  return args;
}

}  // namespace cuttlefish
