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
#include <deque>
#include <regex>
#include <set>
#include <string>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_cmdline_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace selector {

Result<GroupCreationInfo> CreationAnalyzer::Analyze(
    const CreationAnalyzerParam& param, const std::optional<ucred>& credential,
    const InstanceDatabase& instance_database,
    InstanceLockFileManager& instance_lock_file_manager) {
  auto selector_options_parser =
      CF_EXPECT(SelectorFlagsParser::ConductSelectFlagsParser(
          param.selector_args, param.cmd_args, param.envs));
  CreationAnalyzer analyzer(param, credential,
                            std::move(selector_options_parser),
                            instance_database, instance_lock_file_manager);
  auto result = CF_EXPECT(analyzer.Analyze());
  return result;
}

CreationAnalyzer::CreationAnalyzer(
    const CreationAnalyzerParam& param, const std::optional<ucred>& credential,
    SelectorFlagsParser&& selector_options_parser,
    const InstanceDatabase& instance_database,
    InstanceLockFileManager& instance_file_lock_manager)
    : cmd_args_(param.cmd_args),
      envs_(param.envs),
      selector_args_(param.selector_args),
      credential_(credential),
      selector_options_parser_{std::move(selector_options_parser)},
      instance_database_{instance_database},
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

/*
 * Filters out the ids in id_pool that already exist in instance_database
 */
static Result<std::vector<unsigned>> CollectUnusedIds(
    const InstanceDatabase& instance_database,
    std::vector<unsigned>&& id_pool) {
  std::deque<unsigned> collected_ids;
  while (!id_pool.empty()) {
    const auto id = id_pool.back();
    id_pool.pop_back();
    auto subset =
        CF_EXPECT(instance_database.FindInstances(Query{kInstanceIdField, id}));
    CF_EXPECT(subset.size() < 2,
              "Cvd Instance Database has two instances with the id: " << id);
    if (subset.empty()) {
      collected_ids.push_back(id);
    }
  }
  return std::vector<unsigned>{collected_ids.begin(), collected_ids.end()};
}

Result<std::vector<InstanceLockFile>>
CreationAnalyzer::AnalyzeInstanceIdsWithLockInternal() {
  // As this test was done earlier, this line must not fail
  const auto n_instances = selector_options_parser_.RequestedNumInstances();
  auto requested_instance_ids = selector_options_parser_.InstanceIds();
  auto acquired_all_file_locks =
      CF_EXPECT(instance_file_lock_manager_.LockAllAvailable());

  auto id_to_lockfile_map =
      ConstructIdLockFileMap(std::move(acquired_all_file_locks));

  // verify if any of the request IDs is beyond the InstanceFileLockManager
  if (requested_instance_ids) {
    for (auto const id : *requested_instance_ids) {
      CF_EXPECT(Contains(id_to_lockfile_map, id),
                id << " is not allowed by InstanceFileLockManager.");
    }
  }

  std::vector<InstanceLockFile> allocated_ids_with_locks;
  if (requested_instance_ids) {
    CF_EXPECT(!requested_instance_ids->empty(),
              "Instance IDs were specified, so should be one or more.");
    for (const auto id : *requested_instance_ids) {
      CF_EXPECT(Contains(id_to_lockfile_map, id),
                "Instance ID " << id << " lock file can't be locked.");
      auto& lock_file = id_to_lockfile_map.at(id);
      allocated_ids_with_locks.emplace_back(std::move(lock_file));
    }
    return allocated_ids_with_locks;
  }

  /* generate n_instances consecutive ids. For backward compatibility,
   * we prefer n consecutive ids for now.
   */
  std::vector<unsigned> id_pool;
  id_pool.reserve(id_to_lockfile_map.size());
  for (const auto& [id, _] : id_to_lockfile_map) {
    id_pool.emplace_back(id);
  }
  auto unused_id_pool =
      CF_EXPECT(CollectUnusedIds(instance_database_, std::move(id_pool)));
  IdAllocator unique_id_allocator = IdAllocator::New(unused_id_pool);
  auto allocated_ids = unique_id_allocator.UniqueConsecutiveItems(n_instances);
  CF_EXPECT(allocated_ids != std::nullopt, "Unique ID allocation failed.");

  // Picks the lock files according to the ids, and discards the rest
  for (const auto id : *allocated_ids) {
    CF_EXPECT(Contains(id_to_lockfile_map, id),
              "Instance ID " << id << " lock file can't be locked.");
    auto& lock_file = id_to_lockfile_map.at(id);
    allocated_ids_with_locks.emplace_back(std::move(lock_file));
  }
  return allocated_ids_with_locks;
}

static Result<std::vector<PerInstanceInfo>> GenerateInstanceInfo(
    const std::optional<std::vector<std::string>>& per_instance_names_opt,
    std::vector<InstanceLockFile>& instance_file_locks) {
  std::vector<std::string> per_instance_names;
  if (per_instance_names_opt) {
    per_instance_names = per_instance_names_opt.value();
    CF_EXPECT(per_instance_names.size() == instance_file_locks.size());
  } else {
    /*
     * What is generated here is an (per-)instance name:
     *  See: go/cf-naming-clarification
     *
     * A full device name is a group name followed by '-' followed by
     * per-instance name. Also, see instance_record.cpp.
     */
    for (const auto& instance_file_lock : instance_file_locks) {
      per_instance_names.emplace_back(
          std::to_string(instance_file_lock.Instance()));
    }
  }

  std::vector<PerInstanceInfo> instance_info;
  for (int i = 0; i < per_instance_names.size(); i++) {
    InstanceLockFile i_th_file_lock = std::move(instance_file_locks[i]);
    const auto& instance_name = per_instance_names[i];
    instance_info.emplace_back(static_cast<unsigned>(i_th_file_lock.Instance()),
                               instance_name, std::move(i_th_file_lock));
  }
  instance_file_locks.clear();
  return instance_info;
}

Result<std::vector<PerInstanceInfo>>
CreationAnalyzer::AnalyzeInstanceIdsWithLock() {
  auto instance_ids_with_lock = CF_EXPECT(AnalyzeInstanceIdsWithLockInternal());
  auto instance_file_locks = CF_EXPECT(GenerateInstanceInfo(
      selector_options_parser_.PerInstanceNames(), instance_ids_with_lock));
  return instance_file_locks;
}

static bool IsCvdStart(const std::vector<std::string>& args) {
  if (args.empty()) {
    return false;
  }
  if (args[0] == "start") {
    return true;
  }
  return (args.size() > 1 && args[1] == "start");
}

Result<GroupCreationInfo> CreationAnalyzer::Analyze() {
  CF_EXPECT(IsCvdStart(cmd_args_),
            "CreationAnalyzer::Analyze() is for cvd start only.");
  auto instance_info = CF_EXPECT(AnalyzeInstanceIdsWithLock());
  group_name_ = AnalyzeGroupName(instance_info);
  home_ = CF_EXPECT(AnalyzeHome());
  envs_["HOME"] = home_;
  CF_EXPECT(envs_.find(kAndroidHostOut) != envs_.end());
  host_artifacts_path_ = envs_.at(kAndroidHostOut);

  GroupCreationInfo report = {.home = home_,
                              .host_artifacts_path = host_artifacts_path_,
                              .group_name = group_name_,
                              .instances = std::move(instance_info),
                              .args = cmd_args_,
                              .envs = envs_};
  return report;
}

std::string CreationAnalyzer::AnalyzeGroupName(
    const std::vector<PerInstanceInfo>& per_instance_infos) const {
  if (selector_options_parser_.GroupName()) {
    return selector_options_parser_.GroupName().value();
  }
  // auto-generate group name
  std::vector<unsigned> ids;
  for (const auto& per_instance_info : per_instance_infos) {
    ids.emplace_back(per_instance_info.instance_id_);
  }
  std::string base_name = GenDefaultGroupName();
  if (instance_database_.IsEmpty()) {
    // if default group, we simply return base_name, which is "cvd"
    return base_name;
  }
  /* We cannot return simply "cvd" as we do not want duplication in the group
   * name across the instance groups owned by the user. Note that the set of ids
   * are expected to be unique to the user, so we use the ids. If ever the end
   * user happened to have already used the generated name, we did our best, and
   * cvd start will fail with a proper error message.
   */
  return base_name + "_" + android::base::Join(ids, "_");
}

Result<std::string> CreationAnalyzer::AnalyzeHome() const {
  CF_EXPECT(credential_ != std::nullopt,
            "Credential is necessary for cvd start.");
  auto system_wide_home =
      CF_EXPECT(SystemWideUserHome(credential_.value().uid));
  if (envs_.find("HOME") != envs_.end() &&
      envs_.at("HOME") != system_wide_home) {
    // explicitly overridden by the user
    return envs_.at("HOME");
  }
  CF_EXPECT(!group_name_.empty(),
            "To auto-generate HOME, the group name is a must.");
  std::string auto_generated_home{kParentOfDefaultHomeDirectories};
  auto_generated_home.append("/" + group_name_);
  CF_EXPECT(EnsureDirectoryExistsAllTheWay(auto_generated_home));
  return auto_generated_home;
}

}  // namespace selector
}  // namespace cuttlefish
