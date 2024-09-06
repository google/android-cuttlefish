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

#include "host/commands/cvd/selector/creation_analyzer.h"

#include <sys/types.h>

#include <algorithm>
#include <map>
#include <string>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/common_utils.h"

namespace cuttlefish {
namespace selector {

Result<CreationAnalyzer> CreationAnalyzer::Create(
    const CreationAnalyzerParam& param,
    InstanceLockFileManager& instance_lock_file_manager) {
  auto selector_options_parser =
      CF_EXPECT(StartSelectorParser::ConductSelectFlagsParser(
          param.selector_args, param.cmd_args, param.envs));
  return CreationAnalyzer(param, std::move(selector_options_parser),
                          instance_lock_file_manager);
}

CreationAnalyzer::CreationAnalyzer(
    const CreationAnalyzerParam& param,
    StartSelectorParser&& selector_options_parser,
    InstanceLockFileManager& instance_file_lock_manager)
    : envs_(param.envs),
      selector_options_parser_{std::move(selector_options_parser)},
      instance_file_lock_manager_{instance_file_lock_manager} {}

static std::unordered_map<unsigned, InstanceLockFile> ConstructIdLockFileMap(
    std::vector<InstanceLockFile>&& lock_files) {
  std::unordered_map<unsigned, InstanceLockFile> mapping;
  for (auto& lock_file : lock_files) {
    const unsigned id = static_cast<unsigned>(lock_file.Instance());
    mapping.insert({id, std::move(lock_file)});
  }
  lock_files.clear();
  return mapping;
}

Result<std::vector<PerInstanceInfo>>
CreationAnalyzer::AnalyzeInstanceIdsInternal(
    const std::vector<unsigned>& requested_instance_ids) {
  CF_EXPECT(!requested_instance_ids.empty(),
            "Instance IDs were specified, so should be one or more.");
  std::vector<std::string> per_instance_names;
  if (selector_options_parser_.PerInstanceNames()) {
    per_instance_names = *selector_options_parser_.PerInstanceNames();
    CF_EXPECT_EQ(per_instance_names.size(), requested_instance_ids.size());
  } else {
    for (const auto id : requested_instance_ids) {
      per_instance_names.push_back(std::to_string(id));
    }
  }

  std::map<unsigned, std::string> id_name_pairs;
  for (size_t i = 0; i != requested_instance_ids.size(); i++) {
    id_name_pairs[requested_instance_ids.at(i)] = per_instance_names.at(i);
  }

  std::vector<PerInstanceInfo> instance_info;
  bool must_acquire_file_locks = selector_options_parser_.MustAcquireFileLock();
  if (!must_acquire_file_locks) {
    for (const auto& [id, name] : id_name_pairs) {
      instance_info.emplace_back(id, name, cvd::INSTANCE_STATE_STARTING);
    }
    return instance_info;
  }
  auto acquired_all_file_locks =
      CF_EXPECT(instance_file_lock_manager_.LockAllAvailable());
  auto id_to_lockfile_map =
      ConstructIdLockFileMap(std::move(acquired_all_file_locks));
  for (const auto& [id, instance_name] : id_name_pairs) {
    CF_EXPECT(Contains(id_to_lockfile_map, id),
              "Instance ID " << id << " lock file can't be locked.");
    auto& lock_file = id_to_lockfile_map.at(id);
    instance_info.emplace_back(id, instance_name, cvd::INSTANCE_STATE_PREPARING,
                               std::move(lock_file));
  }
  return instance_info;
}

Result<std::vector<PerInstanceInfo>>
CreationAnalyzer::AnalyzeInstanceIdsInternal() {
  CF_EXPECT(selector_options_parser_.MustAcquireFileLock(),
            "For now, cvd server always acquire the file locks "
                << "when IDs are automatically allocated.");

  // As this test was done earlier, this line must not fail
  const auto n_instances = selector_options_parser_.RequestedNumInstances();
  auto acquired_all_file_locks =
      CF_EXPECT(instance_file_lock_manager_.LockAllAvailable());
  auto id_to_lockfile_map =
      ConstructIdLockFileMap(std::move(acquired_all_file_locks));

  /* generate n_instances consecutive ids. For backward compatibility,
   * we prefer n consecutive ids for now.
   */
  std::vector<unsigned> id_pool;
  id_pool.reserve(id_to_lockfile_map.size());
  for (const auto& [id, _] : id_to_lockfile_map) {
    id_pool.push_back(id);
  }
  auto unique_id_allocator = IdAllocator::New(id_pool);
  CF_EXPECT(unique_id_allocator != nullptr,
            "Memory allocation for UniqueResourceAllocator failed.");

  // auto-generation means the user did not specify much: e.g. "cvd start"
  // In this case, the user may expect the instance id to be 1+
  auto allocated_ids_opt =
      unique_id_allocator->UniqueConsecutiveItems(n_instances);
  CF_EXPECT(allocated_ids_opt != std::nullopt, "Unique ID allocation failed.");

  std::vector<unsigned> allocated_ids;
  allocated_ids.reserve(allocated_ids_opt->size());
  for (const auto& reservation : *allocated_ids_opt) {
    allocated_ids.push_back(reservation.Get());
  }
  std::sort(allocated_ids.begin(), allocated_ids.end());

  const auto per_instance_names_opt =
      selector_options_parser_.PerInstanceNames();
  if (per_instance_names_opt) {
    CF_EXPECT(per_instance_names_opt->size() == allocated_ids.size());
  }
  std::vector<PerInstanceInfo> instance_info;
  for (size_t i = 0; i != allocated_ids.size(); i++) {
    const auto id = allocated_ids.at(i);

    std::string name = std::to_string(id);
    // Use the user provided instance name only if it's not empty.
    if (per_instance_names_opt && !(*per_instance_names_opt)[i].empty()) {
      name = (*per_instance_names_opt)[i];
    }
    instance_info.emplace_back(id, name, cvd::INSTANCE_STATE_PREPARING,
                               std::move(id_to_lockfile_map.at(id)));
  }
  return instance_info;
}

Result<std::vector<PerInstanceInfo>> CreationAnalyzer::AnalyzeInstanceIds() {
  auto requested_instance_ids = selector_options_parser_.InstanceIds();
  return requested_instance_ids
             ? CF_EXPECT(AnalyzeInstanceIdsInternal(*requested_instance_ids))
             : CF_EXPECT(AnalyzeInstanceIdsInternal());
}

Result<GroupCreationInfo> CreationAnalyzer::ExtractGroupInfo() {
  auto instance_info = CF_EXPECT(AnalyzeInstanceIds());
  std::vector<unsigned> ids;
  ids.reserve(instance_info.size());
  for (const auto& instance : instance_info) {
    ids.push_back(instance.instance_id_);
  }
  auto group_info = CF_EXPECT(ExtractGroup(instance_info));

  auto home = CF_EXPECT(AnalyzeHome());

  auto android_host_out = CF_EXPECT(AndroidHostPath(envs_));
  std::string android_product_out_path = Contains(envs_, kAndroidProductOut)
                                             ? envs_.at(kAndroidProductOut)
                                             : android_host_out;
  return GroupCreationInfo{
      .home = home,
      .host_artifacts_path = android_host_out,
      .product_out_path = android_product_out_path,
      .group_name = group_info.group_name,
      .instances = std::move(instance_info),
  };
}

Result<CreationAnalyzer::GroupInfo> CreationAnalyzer::ExtractGroup(
    const std::vector<PerInstanceInfo>& per_instance_infos) const {
  CreationAnalyzer::GroupInfo group_name_info = {
      // With an empty group name the instance manager will pick one guaranteed
      // to be unique.
      .group_name = selector_options_parser_.GroupName().value_or(""),
      .default_group = false};
  return group_name_info;
}

Result<std::string> CreationAnalyzer::AnalyzeHome() const {
  auto system_wide_home = CF_EXPECT(SystemWideUserHome());
  if (Contains(envs_, "HOME") && envs_.at("HOME") != system_wide_home) {
    return envs_.at("HOME");
  }

  // TODO(jemoreira): use the group name for this directory
  std::string auto_generated_home = DefaultBaseDir() + "/home";
  CF_EXPECT(EnsureDirectoryExists(auto_generated_home));
  return auto_generated_home;
}

}  // namespace selector
}  // namespace cuttlefish
