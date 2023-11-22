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

#include "host/commands/cvd/selector/instance_database_utils.h"

#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "common/libs/utils/files.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {
namespace selector {

Result<std::string> GetCuttlefishConfigPath(const std::string& home) {
  std::string home_realpath;
  CF_EXPECT(DirectoryExists(home), "Invalid Home Directory");
  CF_EXPECT(android::base::Realpath(home, &home_realpath));
  static const char kSuffix[] = "/cuttlefish_assembly/cuttlefish_config.json";
  std::string config_path = AbsolutePath(home_realpath + kSuffix);
  CF_EXPECT(FileExists(config_path), "No config file exists");
  return {config_path};
}

std::string GenInternalGroupName() {
  std::string_view internal_name{kCvdNamePrefix};  // "cvd-"
  internal_name.remove_suffix(1);                  // "cvd"
  return std::string(internal_name);
}

std::string GenDefaultGroupName() { return GenInternalGroupName(); }

std::string LocalDeviceNameRule(const std::string& group_name,
                                const std::string& instance_name) {
  return group_name + "-" + instance_name;
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
  auto pieces = android::base::Split(token, "-");
  for (const auto& piece : pieces) {
    if (!std::regex_match(piece, base_regular_expr)) {
      return false;
    }
  }
  return true;
}

Result<DeviceName> BreakDeviceName(const std::string& device_name) {
  CF_EXPECT(!device_name.empty());
  CF_EXPECT(Contains(device_name, '-'));
  auto dash_pos = device_name.find_first_of('-');
  // - must be neither the first nor the last character
  CF_EXPECT(dash_pos != 0 && dash_pos != (device_name.size() - 1));
  const auto group_name = device_name.substr(0, dash_pos);
  const auto instance_name = device_name.substr(dash_pos + 1);
  return DeviceName{.group_name = group_name,
                    .per_instance_name = instance_name};
}

bool IsValidDeviceName(const std::string& token) {
  if (token.empty()) {
    return false;
  }
  auto result = BreakDeviceName(token);
  if (!result.ok()) {
    return false;
  }
  const auto [group_name, instance_name] = *result;
  return IsValidGroupName(group_name) && IsValidInstanceName(instance_name);
}

bool PotentiallyHostArtifactsPath(const std::string& host_artifacts_path) {
  if (host_artifacts_path.empty() || !DirectoryExists(host_artifacts_path)) {
    return false;
  }
  const auto host_bin_path = host_artifacts_path + "/bin";
  auto contents_result = DirectoryContents(host_bin_path);
  if (!contents_result.ok()) {
    return false;
  }
  std::vector<std::string> contents = std::move(*contents_result);
  std::set<std::string> contents_set{std::move_iterator(contents.begin()),
                                     std::move_iterator(contents.end())};
  std::set<std::string> launchers = {"cvd", "launch_cvd"};
  std::vector<std::string> result;
  std::set_intersection(launchers.cbegin(), launchers.cend(),
                        contents_set.cbegin(), contents_set.cend(),
                        std::back_inserter(result));
  return !result.empty();
}

std::string GenerateTooManyInstancesErrorMsg(const int n,
                                             const std::string& field_name) {
  std::stringstream s;
  s << "Only up to " << n << " must match";
  if (!field_name.empty()) {
    s << " by the field " << field_name;
  }
  return s.str();
}

}  // namespace selector
}  // namespace cuttlefish
