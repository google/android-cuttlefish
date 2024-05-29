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
#include <set>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace {

constexpr char kDynamicPartitions[] = "dynamic_partition_list";
constexpr char kGoogleDynamicPartitions[] = "google_dynamic_partitions";
constexpr char kSuperBlockDevices[] = "super_block_devices";
constexpr char kSuperPartitionGroups[] = "super_partition_groups";
constexpr char kUseDynamicPartitions[] = "use_dynamic_partitions";

Result<std::string> GetExpected(const MiscInfo& misc_info,
                                const std::string& key) {
  auto lookup = misc_info.find(key);
  CF_EXPECTF(lookup != misc_info.end(),
             "Unable to retrieve expected value from key: {}", key);
  return lookup->second;
}

std::string MergePartitionLists(const std::string& vendor,
                                const std::string& system,
                                const std::set<std::string>& extracted_images) {
  const std::string full_string = fmt::format("{} {}", vendor, system);
  const std::vector<std::string> full_list =
      android::base::Tokenize(full_string, " ");
  // std::set removes duplicates and orders the elements, which we want
  const std::set<std::string> full_set(full_list.begin(), full_list.end());
  std::set<std::string> filtered_set;
  std::set_intersection(full_set.cbegin(), full_set.cend(),
                        extracted_images.cbegin(), extracted_images.cend(),
                        std::inserter(filtered_set, filtered_set.begin()));
  return android::base::Join(filtered_set, " ");
}

std::string GetPartitionList(const MiscInfo& vendor_info,
                             const MiscInfo& system_info,
                             const std::string& key,
                             const std::set<std::string>& extracted_images) {
  std::string vendor_list = GetExpected(vendor_info, key).value_or("");
  std::string system_list = GetExpected(system_info, key).value_or("");
  return MergePartitionLists(vendor_list, system_list, extracted_images);
}

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

// based on build/make/tools/releasetools/merge/merge_target_files.py
Result<MiscInfo> GetCombinedDynamicPartitions(
    const MiscInfo& vendor_info, const MiscInfo& system_info,
    const std::set<std::string>& extracted_images) {
  auto vendor_use_dp =
      CF_EXPECT(GetExpected(vendor_info, kUseDynamicPartitions));
  CF_EXPECTF(vendor_use_dp == "true", "Vendor build must have {}=true",
             kUseDynamicPartitions);
  auto system_use_dp =
      CF_EXPECT(GetExpected(system_info, kUseDynamicPartitions));
  CF_EXPECTF(system_use_dp == "true", "System build must have {}=true",
             kUseDynamicPartitions);
  MiscInfo result;
  // copy where both files are equal
  for (const auto& key_val : vendor_info) {
    const auto value_result = GetExpected(system_info, key_val.first);
    if (value_result.ok() && *value_result == key_val.second) {
      result[key_val.first] = key_val.second;
    }
  }

  result[kDynamicPartitions] = GetPartitionList(
      vendor_info, system_info, kDynamicPartitions, extracted_images);

  const auto block_devices_result =
      GetExpected(vendor_info, kSuperBlockDevices);
  if (block_devices_result.ok()) {
    result[kSuperBlockDevices] = *block_devices_result;
    for (const auto& block_device :
         android::base::Tokenize(result[kSuperBlockDevices], " ")) {
      const auto key = "super_" + block_device + "_device_size";
      result[key] = CF_EXPECT(GetExpected(vendor_info, key));
    }
  }

  result[kSuperPartitionGroups] =
      CF_EXPECT(GetExpected(vendor_info, kSuperPartitionGroups));
  for (const auto& group :
       android::base::Tokenize(result[kSuperPartitionGroups], " ")) {
    const auto group_size_key = "super_" + group + "_group_size";
    result[group_size_key] =
        CF_EXPECT(GetExpected(vendor_info, group_size_key));

    const auto partition_list_key = "super_" + group + "_partition_list";
    result[partition_list_key] = GetPartitionList(
        vendor_info, system_info, partition_list_key, extracted_images);
  }

  // TODO(chadreynolds): add vabc_cow_version logic if we need to support older
  // builds
  for (const auto& key :
       {"virtual_ab", "virtual_ab_retrofit", "lpmake", "super_metadata_device",
        "super_partition_error_limit", "super_partition_size"}) {
    const auto value_result = GetExpected(vendor_info, key);
    if (value_result.ok()) {
      result[key] = *value_result;
    }
  }
  return std::move(result);
}

void MergeInKeys(const MiscInfo& source, MiscInfo& target) {
  for (const auto& key_val : source) {
    target[key_val.first] = key_val.second;
  }
}

} // namespace cuttlefish
