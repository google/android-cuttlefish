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
#include <array>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace {

constexpr char kDynamicPartitions[] = "dynamic_partition_list";
constexpr char kGoogleDynamicPartitions[] = "google_dynamic_partitions";
constexpr char kRollbackIndexSuffix[] = "_rollback_index_location";
constexpr char kSuperBlockDevices[] = "super_block_devices";
constexpr char kSuperPartitionGroups[] = "super_partition_groups";
constexpr char kUseDynamicPartitions[] = "use_dynamic_partitions";
constexpr std::array kNonPartitionKeysToMerge = {
    "ab_update", "default_system_dev_certificate"};

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

std::vector<std::string> GeneratePartitionKeys(const std::string& name) {
  std::vector<std::string> result;
  result.emplace_back("avb_" + name);
  result.emplace_back("avb_" + name + "_algorithm");
  result.emplace_back("avb_" + name + "_key_path");
  result.emplace_back("avb_" + name + kRollbackIndexSuffix);
  result.emplace_back("avb_" + name + "_hashtree_enable");
  result.emplace_back("avb_" + name + "_add_hashtree_footer_args");
  result.emplace_back(name + "_disable_sparse");
  result.emplace_back("building_" + name + "_image");
  auto fs_type_key = name + "_fs_type";
  if (name == "system") {
    fs_type_key = "fs_type";
  }
  result.emplace_back(fs_type_key);
  return result;
}

Result<int> ResolveRollbackIndexConflicts(
    const std::string& index_string,
    const std::unordered_set<int> used_indices) {
  int index;
  CF_EXPECTF(android::base::ParseInt(index_string, &index),
             "Unable to parse value {} to string.  Maybe a wrong or bad value "
             "read for the rollback index?",
             index_string);
  while (Contains(used_indices, index)) {
    ++index;
  }
  return index;
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

Result<void> WriteMiscInfo(const MiscInfo& misc_info,
                           const std::string& output_path) {
  std::stringstream file_content;
  for (const auto& entry : misc_info) {
    file_content << entry.first << "=" << entry.second << "\n";
  }

  SharedFD output_file = SharedFD::Creat(output_path.c_str(), 0644);
  CF_EXPECT(output_file->IsOpen(),
            "Failed to open output misc file: " << output_file->StrError());

  CF_EXPECT(
      WriteAll(output_file, file_content.str()) >= 0,
      "Failed to write output misc file contents: " << output_file->StrError());
  return {};
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

Result<MiscInfo> MergeMiscInfos(
    const MiscInfo& vendor_info, const MiscInfo& system_info,
    const MiscInfo& combined_dp_info,
    const std::vector<std::string>& system_partitions) {
  // the combined misc info uses the vendor values as defaults
  MiscInfo result = vendor_info;
  std::unordered_set<int> used_indices;
  for (const auto& partition : system_partitions) {
    for (const auto& key : GeneratePartitionKeys(partition)) {
      if (!Contains(system_info, key)) {
        continue;
      }
      auto system_value = system_info.find(key)->second;
      // avb_<partition>_rollback_index_location values can conflict across
      // different builds
      if (android::base::EndsWith(key, kRollbackIndexSuffix)) {
        const auto index = CF_EXPECT(
            ResolveRollbackIndexConflicts(system_value, used_indices));
        used_indices.insert(index);
        system_value = std::to_string(index);
      }
      result[key] = system_value;
    }
  }
  for (const auto& key : kNonPartitionKeysToMerge) {
    if (Contains(system_info, key)) {
      result[key] = system_info.find(key)->second;
    }
  }
  for (const auto& key_val : combined_dp_info) {
    result[key_val.first] = key_val.second;
  }
  return std::move(result);
}

} // namespace cuttlefish
