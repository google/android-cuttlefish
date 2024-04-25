//
// Copyright (C) 2020 The Android Open Source Project
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

#include "misc_info.h"

#include <algorithm>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace {

constexpr char kDynamicPartitions[] = "dynamic_partition_list";
constexpr char kGoogleDynamicPartitions[] = "google_dynamic_partitions";
constexpr char kSuperPartitionGroups[] = "super_partition_groups";

}  // namespace

Result<MiscInfo> ParseMiscInfo(const std::string& misc_info_contents) {
  auto lines = android::base::Split(misc_info_contents, "\n");
  MiscInfo misc_info;
  for (auto& line : lines) {
    line = android::base::Trim(line);
    if (line.size() == 0) {
      continue;
    }
    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      LOG(WARNING) << "Line in unknown format: \"" << line << "\"";
      continue;
    }
    // Not using android::base::Split here to only capture the first =
    const auto key = android::base::Trim(line.substr(0, eq_pos));
    const auto value = android::base::Trim(line.substr(eq_pos + 1));
    const bool duplicate = Contains(misc_info, key) && misc_info[key] != value;
    CF_EXPECTF(!duplicate,
               "Duplicate key with different value. key:\"{}\", previous "
               "value:\"{}\", this value:\"{}\"",
               key, misc_info[key], value);
    misc_info[key] = value;
  }
  return misc_info;
}

std::string WriteMiscInfo(const MiscInfo& misc_info) {
  std::stringstream out;
  for (const auto& entry : misc_info) {
    out << entry.first << "=" << entry.second << "\n";
  }
  return out.str();
}

std::vector<std::string> SuperPartitionComponents(const MiscInfo& info) {
  auto value_it = info.find(kDynamicPartitions);
  if (value_it == info.end()) {
    return {};
  }
  auto components = android::base::Split(value_it->second, " ");
  for (auto& component : components) {
    component = android::base::Trim(component);
  }
  components.erase(std::remove(components.begin(), components.end(), ""),
                   components.end());
  return components;
}

bool SetSuperPartitionComponents(const std::vector<std::string>& components,
                                 MiscInfo* misc_info) {
  auto super_partition_groups = misc_info->find(kSuperPartitionGroups);
  if (super_partition_groups == misc_info->end()) {
    LOG(ERROR) << "Failed to find super partition groups in misc_info";
    return false;
  }

  // Remove all existing update groups in misc_info
  auto update_groups =
      android::base::Split(super_partition_groups->second, " ");
  for (const auto& group_name : update_groups) {
    auto partition_list = android::base::StringPrintf("super_%s_partition_list",
                                                      group_name.c_str());
    auto partition_size =
        android::base::StringPrintf("super_%s_group_size", group_name.c_str());
    for (const auto& key : {partition_list, partition_size}) {
      auto it = misc_info->find(key);
      if (it == misc_info->end()) {
        LOG(ERROR) << "Failed to find " << key << " in misc_info";
        return false;
      }
      misc_info->erase(it);
    }
  }

  // For merged target-file, put all dynamic partitions under the
  // google_dynamic_partitions update group.
  // TODO(xunchang) use different update groups for system and vendor images.
  (*misc_info)[kDynamicPartitions] = android::base::Join(components, " ");
  (*misc_info)[kSuperPartitionGroups] = kGoogleDynamicPartitions;
  std::string partitions_list_key = android::base::StringPrintf(
      "super_%s_partition_list", kGoogleDynamicPartitions);
  (*misc_info)[partitions_list_key] = android::base::Join(components, " ");

  // Use the entire super partition as the group size
  std::string group_size_key = android::base::StringPrintf(
      "super_%s_group_size", kGoogleDynamicPartitions);
  auto super_size_it = misc_info->find("super_partition_size");
  if (super_size_it == misc_info->end()) {
    LOG(ERROR) << "Failed to find super partition size";
    return false;
  }
  (*misc_info)[group_size_key] = super_size_it->second;
  return true;
}

} // namespace cuttlefish
