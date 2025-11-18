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

#include "cuttlefish/host/commands/cvd/cli/selector/creation_analyzer.h"

#include <sys/types.h>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/unique_resource_allocator.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cli/selector/start_selector_parser.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"

namespace cuttlefish {
namespace selector {
namespace {

class CreationAnalyzer {
 public:
  static Result<CreationAnalyzer> Create(const CreationAnalyzerParam& param,
                                         InstanceLockFileManager&);

  Result<GroupCreationInfo> ExtractGroupInfo();

 private:
  using IdAllocator = UniqueResourceAllocator<unsigned>;

  CreationAnalyzer(const CreationAnalyzerParam& param,
                   StartSelectorParser&& selector_options_parser,
                   InstanceLockFileManager& instance_lock_file_manager);

  /**
   * calculate n_instances_ and instance_ids_
   */
  Result<std::vector<InstanceLockFile>> AnalyzeInstanceIds();
  Result<std::vector<InstanceParams>> AnalyzeInstances(
      const std::vector<unsigned>& instance_ids);

  /**
   * Figures out the HOME directory
   *
   * The issue is that many times, HOME is anyway implicitly given. Thus, only
   * if the HOME value is not equal to the HOME directory recognized by the
   * system, it can be safely regarded as overridden by the user.
   *
   * If that is not the case, we use an automatically generated value as HOME.
   */
  Result<std::optional<std::string>> AnalyzeHome() const;

  Result<std::vector<InstanceLockFile>> AnalyzeInstanceIdsInternal();
  Result<std::vector<InstanceLockFile>> AnalyzeInstanceIdsInternal(
      const std::vector<unsigned>& requested_instance_ids);

  // inputs
  std::unordered_map<std::string, std::string> envs_;

  // internal, temporary
  StartSelectorParser selector_options_parser_;
  InstanceLockFileManager& instance_lock_file_manager_;
};

Result<CreationAnalyzer> CreationAnalyzer::Create(
    const CreationAnalyzerParam& param,
    InstanceLockFileManager& instance_lock_file_manager) {
  auto selector_options_parser =
      CF_EXPECT(StartSelectorParser::ConductSelectFlagsParser(
          param.selectors, param.cmd_args, param.envs));
  return CreationAnalyzer(param, std::move(selector_options_parser),
                          instance_lock_file_manager);
}

CreationAnalyzer::CreationAnalyzer(
    const CreationAnalyzerParam& param,
    StartSelectorParser&& selector_options_parser,
    InstanceLockFileManager& instance_lock_file_manager)
    : envs_(param.envs),
      selector_options_parser_{std::move(selector_options_parser)},
      instance_lock_file_manager_{instance_lock_file_manager} {}

static std::unordered_map<unsigned, InstanceLockFile> ConstructIdLockFileMap(
    std::set<InstanceLockFile>&& lock_files) {
  std::unordered_map<unsigned, InstanceLockFile> mapping;
  for (auto& lock_file : lock_files) {
    const unsigned id = static_cast<unsigned>(lock_file.Instance());
    mapping.insert({id, std::move(lock_file)});
  }
  lock_files.clear();
  return mapping;
}

Result<std::vector<InstanceLockFile>>
CreationAnalyzer::AnalyzeInstanceIdsInternal(
    const std::vector<unsigned>& requested_instance_ids) {
  CF_EXPECT(!requested_instance_ids.empty(),
            "Instance IDs were specified, so should be one or more.");

  std::set<int> requested(requested_instance_ids.begin(),
                          requested_instance_ids.end());
  auto acquired_all_file_locks =
      CF_EXPECT(instance_lock_file_manager_.TryAcquireLocks(requested));
  auto id_to_lockfile_map =
      ConstructIdLockFileMap(std::move(acquired_all_file_locks));
  std::vector<InstanceLockFile> instance_locks;
  for (const auto id : requested_instance_ids) {
    CF_EXPECT(Contains(id_to_lockfile_map, id),
              "Instance ID " << id << " lock file can't be locked.");
    auto& lock_file = id_to_lockfile_map.at(id);
    instance_locks.emplace_back(std::move(lock_file));
  }
  return instance_locks;
}

Result<std::vector<InstanceLockFile>>
CreationAnalyzer::AnalyzeInstanceIdsInternal() {
  // As this test was done earlier, this line must not fail
  const auto n_instances = selector_options_parser_.RequestedNumInstances();
  auto acquired_all_file_locks =
      CF_EXPECT(instance_lock_file_manager_.AcquireUnusedLocks(n_instances));
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
  std::vector<InstanceLockFile> instance_locks;
  for (size_t i = 0; i != allocated_ids.size(); i++) {
    const auto id = allocated_ids.at(i);

    instance_locks.emplace_back(std::move(id_to_lockfile_map.at(id)));
  }
  return instance_locks;
}

Result<std::vector<InstanceLockFile>> CreationAnalyzer::AnalyzeInstanceIds() {
  auto requested_instance_ids = selector_options_parser_.InstanceIds();
  return requested_instance_ids
             ? CF_EXPECT(AnalyzeInstanceIdsInternal(*requested_instance_ids))
             : CF_EXPECT(AnalyzeInstanceIdsInternal());
}

Result<std::vector<InstanceParams>> CreationAnalyzer::AnalyzeInstances(
    const std::vector<unsigned>& instance_ids) {
  std::optional<std::vector<std::string>> instance_names_opt =
      selector_options_parser_.PerInstanceNames();
  std::vector<InstanceParams> instance_params;
  for (size_t i = 0; i != instance_ids.size(); i++) {
    instance_params.emplace_back(
        InstanceParams{.instance_id = instance_ids.at(i)});
  }

  if (instance_names_opt) {
    CF_EXPECT_EQ(instance_names_opt.value().size(), instance_ids.size(),
                 "Number of instance names provided doesn't match number of "
                 "acquired instance ids");
    for (size_t i = 0; i < instance_params.size(); ++i) {
      instance_params[i].per_instance_name = instance_names_opt.value()[i];
    }
  }

  return instance_params;
}

Result<GroupCreationInfo> CreationAnalyzer::ExtractGroupInfo() {
  InstanceGroupParams group_params;
  std::vector<InstanceLockFile> instance_file_locks =
      CF_EXPECT(AnalyzeInstanceIds());
  std::vector<unsigned> instance_ids;
  for (const auto& instance_file_lock : instance_file_locks) {
    instance_ids.emplace_back(instance_file_lock.Instance());
  }
  group_params.instances = CF_EXPECT(AnalyzeInstances(instance_ids));
  group_params.group_name = selector_options_parser_.GroupName().value_or("");
  InstanceManager::GroupDirectories group_directories{
      .home = CF_EXPECT(AnalyzeHome()),
      .host_artifacts_path = CF_EXPECT(AndroidHostPath(envs_)),
  };
  size_t num_instances = group_params.instances.size();
  group_directories.product_out_paths.reserve(num_instances);
  auto it = envs_.find(kAndroidProductOut);
  if (it != envs_.end()) {
    std::vector<std::string> env_product_out =
        android::base::Split(it->second, ",");
    if (env_product_out.size() > num_instances) {
      LOG(WARNING) << env_product_out.size()
                   << " product paths provided, but only " << num_instances
                   << " are going to be created";
      env_product_out.resize(num_instances);
    }
    for (auto& env_path : env_product_out) {
      group_directories.product_out_paths.emplace_back(env_path);
    }
  } else {
    group_directories.product_out_paths.emplace_back(
        group_directories.host_artifacts_path);
  }
  while (group_directories.product_out_paths.size() < num_instances) {
    // Use the first product path when more instances are required than product
    // paths provided. This supports creating multiple identical instances from
    // a single set of images.
    group_directories.product_out_paths.emplace_back(
        group_directories.product_out_paths[0]);
  }

  return GroupCreationInfo{
      .group_creation_params = group_params,
      .group_directories = group_directories,
      .instance_file_locks = instance_file_locks,
  };
}  // namespace

Result<std::optional<std::string>> CreationAnalyzer::AnalyzeHome() const {
  auto home_it = envs_.find("HOME");
  if (home_it == envs_.end() ||
      home_it->second == CF_EXPECT(SystemWideUserHome())) {
    return std::nullopt;
  }
  std::string home = home_it->second;
  CF_EXPECT(EnsureDirectoryExists(home),
            "Provided home directory doesn't exist and can't be created");
  return home;
}

}  // namespace

Result<GroupCreationInfo> AnalyzeCreation(
    const CreationAnalyzerParam& params,
    InstanceLockFileManager& lock_file_manager) {
  CreationAnalyzer analyzer =
      CF_EXPECT(CreationAnalyzer::Create(params, lock_file_manager));
  return CF_EXPECT(analyzer.ExtractGroupInfo());
}

}  // namespace selector
}  // namespace cuttlefish
