/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/instances/instance_database_utils.h"

#include <regex>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/files.h"

namespace cuttlefish {

Result<std::string> GetCuttlefishConfigPath(const std::string& home) {
  std::string home_realpath;
  CF_EXPECT(DirectoryExists(home), "Invalid Home Directory");
  CF_EXPECT(android::base::Realpath(home, &home_realpath));
  static const char kSuffix[] = "/cuttlefish_assembly/cuttlefish_config.json";
  std::string config_path = AbsolutePath(home_realpath + kSuffix);
  CF_EXPECT(FileExists(config_path), "No config file exists");
  return {config_path};
}

bool IsValidGroupName(const std::string& token) {
  std::regex regular_expr("[A-Za-z_][A-Za-z_0-9]*");
  return std::regex_match(token, regular_expr);
}

bool IsValidInstanceName(const std::string& token) {
  if (token.empty()) {
    return true;
  }
  std::regex base_regular_expr("[A-Za-z_0-9]+");
  std::vector<std::string> pieces = android::base::Split(token, "-");
  for (const std::string& piece : pieces) {
    if (!std::regex_match(piece, base_regular_expr)) {
      return false;
    }
  }
  return true;
}

Result<DeviceName> BreakDeviceName(const std::string& device_name) {
  CF_EXPECT(!device_name.empty());
  size_t dash_pos = device_name.find_first_of('-');
  CF_EXPECTF(dash_pos != std::string::npos, "No '-' in '{}'", device_name);
  // - must be neither the first nor the last character
  CF_EXPECT(dash_pos != 0 && dash_pos != (device_name.size() - 1));
  const std::string group_name = device_name.substr(0, dash_pos);
  const std::string instance_name = device_name.substr(dash_pos + 1);
  return DeviceName{.group_name = group_name,
                    .per_instance_name = instance_name};
}

bool IsValidDeviceName(const std::string& token) {
  if (token.empty()) {
    return false;
  }
  Result<DeviceName> result = BreakDeviceName(token);
  if (!result.ok()) {
    return false;
  }
  const auto [group_name, instance_name] = *result;
  return IsValidGroupName(group_name) && IsValidInstanceName(instance_name);
}

}  // namespace cuttlefish
