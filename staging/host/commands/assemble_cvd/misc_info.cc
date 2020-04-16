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

#include <android-base/logging.h>
#include <android-base/strings.h>

MiscInfo ParseMiscInfo(const std::string& misc_info_contents) {
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
    auto key = android::base::Trim(line.substr(0, eq_pos));
    auto value = android::base::Trim(line.substr(eq_pos + 1));
    if (misc_info.find(key) != misc_info.end() && misc_info[key] != value) {
      LOG(ERROR) << "Duplicate key: \"" << key << "\". First value: \""
                 << misc_info[key] << "\", Second value: \"" << value << "\"";
      return {};
    }
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

static const std::string kDynamicPartitions = "dynamic_partition_list";

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

static const std::string kGoogleDynamicPartitions =
    "super_google_dynamic_partitions_partition_list";

void SetSuperPartitionComponents(const std::vector<std::string>& components,
                                 MiscInfo* misc_info) {
  (*misc_info)[kDynamicPartitions] = android::base::Join(components, " ");
  (*misc_info)[kGoogleDynamicPartitions] = android::base::Join(components, " ");
}
