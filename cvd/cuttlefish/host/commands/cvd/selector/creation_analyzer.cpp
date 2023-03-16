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
#include <regex>
#include <set>
#include <string>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace selector {

static bool IsCvdStart(const std::string& cmd) {
  if (cmd.empty()) {
    return false;
  }
  return cmd == "start";
}

Result<GroupCreationInfo> CreationAnalyzer::Analyze(
    const std::string& cmd, const CreationAnalyzerParam& param,
    const ucred& credential, const InstanceDatabase& instance_database,
    InstanceLockFileManager& instance_lock_file_manager) {
  CF_EXPECT(IsCvdStart(cmd),
            "CreationAnalyzer::Analyze() is for cvd start only.");
  const auto client_uid = credential.uid;
  auto selector_options_parser =
      CF_EXPECT(StartSelectorParser::ConductSelectFlagsParser(
          client_uid, param.selector_args, param.cmd_args, param.envs));
  CreationAnalyzer analyzer(param, credential,
                            std::move(selector_options_parser),
                            instance_database, instance_lock_file_manager);
  auto result = CF_EXPECT(analyzer.Analyze());
  return result;
}

CreationAnalyzer::CreationAnalyzer(
    const CreationAnalyzerParam& param, const ucred& credential,
    StartSelectorParser&& selector_options_parser,
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

static Result<void> IsIdAvailable(const InstanceDatabase& instance_database,
                                  const unsigned id) {
  auto subset =
      CF_EXPECT(instance_database.FindInstances(Query{kInstanceIdField, id}));
  CF_EXPECT(subset.empty());
  return {};
}

Result<std::vector<PerInstanceInfo>>
CreationAnalyzer::AnalyzeInstanceIdsInternal(
    const std::vector<unsigned>& requested_instance_ids) {
  CF_EXPECT(!requested_instance_ids.empty(),
            "Instance IDs were specified, so should be one or more.");
  for (const auto id : requested_instance_ids) {
    CF_EXPECT(IsIdAvailable(instance_database_, id),
              "instance ID #" << id << " is requeested but not available.");
  }

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
      instance_info.emplace_back(id, name);
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
    instance_info.emplace_back(id, instance_name, std::move(lock_file));
  }
  return instance_info;
}

/*
 * Filters out the ids in id_pool that already exist in instance_database
 */
static Result<std::vector<unsigned>> CollectUnusedIds(
    const InstanceDatabase& instance_database,
    std::vector<unsigned>&& id_pool) {
  std::vector<unsigned> collected_ids;
  for (const auto id : id_pool) {
    if (IsIdAvailable(instance_database, id).ok()) {
      collected_ids.push_back(id);
    }
  }
  return collected_ids;
}

struct NameLockFilePair {
  std::string name;
  InstanceLockFile lock_file;
};
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
  auto unused_id_pool =
      CF_EXPECT(CollectUnusedIds(instance_database_, std::move(id_pool)));
  auto unique_id_allocator = std::move(IdAllocator::New(unused_id_pool));
  CF_EXPECT(unique_id_allocator != nullptr,
            "Memory allocation for UniqueResourceAllocator failed.");

  // auto-generation means the user did not specify much: e.g. "cvd start"
  // In this case, the user may expect the instance id to be 1+
  using ReservationSet = UniqueResourceAllocator<unsigned>::ReservationSet;
  std::optional<ReservationSet> allocated_ids_opt;
  if (selector_options_parser_.IsMaybeDefaultGroup()) {
    allocated_ids_opt = unique_id_allocator->TakeRange(1, 1 + n_instances);
  }
  if (!allocated_ids_opt) {
    allocated_ids_opt =
        unique_id_allocator->UniqueConsecutiveItems(n_instances);
  }
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
    std::string name = (per_instance_names_opt ? per_instance_names_opt->at(i)
                                               : std::to_string(id));
    instance_info.emplace_back(id, name, std::move(id_to_lockfile_map.at(id)));
  }
  return instance_info;
}

Result<std::vector<PerInstanceInfo>> CreationAnalyzer::AnalyzeInstanceIds() {
  auto requested_instance_ids = selector_options_parser_.InstanceIds();
  return requested_instance_ids
             ? CF_EXPECT(AnalyzeInstanceIdsInternal(*requested_instance_ids))
             : CF_EXPECT(AnalyzeInstanceIdsInternal());
}

/*
 * 1. Remove --num_instances, --instance_nums, --base_instance_num if any.
 * 2. If the ids are consecutive and ordered, add:
 *   --base_instance_num=min --num_instances=ids.size()
 * 3. If not, --instance_nums=<ids>
 *
 */
static Result<std::vector<std::string>> UpdateInstanceArgs(
    std::vector<std::string>&& args, const std::vector<unsigned>& ids) {
  CF_EXPECT(ids.empty() == false);

  std::vector<std::string> new_args{std::move(args)};
  std::string old_instance_nums;
  std::string old_num_instances;
  std::string old_base_instance_num;

  std::vector<Flag> instance_id_flags{
      GflagsCompatFlag("instance_nums", old_instance_nums),
      GflagsCompatFlag("num_instances", old_num_instances),
      GflagsCompatFlag("base_instance_num", old_base_instance_num)};
  // discard old ones
  ParseFlags(instance_id_flags, new_args);

  auto max = *(std::max_element(ids.cbegin(), ids.cend()));
  auto min = *(std::min_element(ids.cbegin(), ids.cend()));

  const bool is_consecutive = ((max - min) == (ids.size() - 1));
  const bool is_sorted = std::is_sorted(ids.begin(), ids.end());

  if (!is_consecutive || !is_sorted) {
    std::string flag_value = android::base::Join(ids, ",");
    new_args.push_back("--instance_nums=" + flag_value);
    return new_args;
  }

  // sorted and consecutive, so let's use old flags
  // like --num_instances and --base_instance_num
  new_args.push_back("--num_instances=" + std::to_string(ids.size()));
  new_args.push_back("--base_instance_num=" + std::to_string(min));
  return new_args;
}

Result<std::vector<std::string>> CreationAnalyzer::UpdateWebrtcDeviceId(
    std::vector<std::string>&& args,
    const std::vector<PerInstanceInfo>& per_instance_info) {
  std::vector<std::string> new_args{std::move(args)};
  std::string flag_value;
  std::vector<Flag> webrtc_device_id_flag{
      GflagsCompatFlag("webrtc_device_id", flag_value)};
  std::vector<std::string> copied_args{new_args};
  CF_EXPECT(ParseFlags(webrtc_device_id_flag, copied_args));

  if (!flag_value.empty()) {
    return new_args;
  }

  CF_EXPECT(!group_name_.empty());
  std::vector<std::string> device_name_list;
  for (const auto& instance : per_instance_info) {
    const auto& per_instance_name = instance.per_instance_name_;
    std::string device_name = group_name_ + "-" + per_instance_name;
    device_name_list.push_back(device_name);
  }
  // take --webrtc_device_id flag away
  new_args = std::move(copied_args);
  new_args.push_back("--webrtc_device_id=" +
                     android::base::Join(device_name_list, ","));
  return new_args;
}

Result<GroupCreationInfo> CreationAnalyzer::Analyze() {
  auto instance_info = CF_EXPECT(AnalyzeInstanceIds());
  std::vector<unsigned> ids;
  ids.reserve(instance_info.size());
  for (const auto& instance : instance_info) {
    ids.push_back(instance.instance_id_);
  }
  cmd_args_ = CF_EXPECT(UpdateInstanceArgs(std::move(cmd_args_), ids));

  group_name_ = CF_EXPECT(AnalyzeGroupName(instance_info));
  cmd_args_ =
      CF_EXPECT(UpdateWebrtcDeviceId(std::move(cmd_args_), instance_info));

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

Result<std::string> CreationAnalyzer::AnalyzeGroupName(
    const std::vector<PerInstanceInfo>& per_instance_infos) const {
  if (selector_options_parser_.GroupName()) {
    return selector_options_parser_.GroupName().value();
  }
  // auto-generate group name
  std::vector<unsigned> ids;
  ids.reserve(per_instance_infos.size());
  for (const auto& per_instance_info : per_instance_infos) {
    ids.push_back(per_instance_info.instance_id_);
  }
  std::string base_name = GenDefaultGroupName();
  if (selector_options_parser_.IsMaybeDefaultGroup()) {
    /*
     * this base_name might be already taken. In that case, the user's
     * request should fail in the InstanceDatabase
     */
    auto groups =
        CF_EXPECT(instance_database_.FindGroups({kGroupNameField, base_name}));
    CF_EXPECT(groups.empty(), "The default instance group name, \""
                                  << base_name << "\" has been already taken.");
    return base_name;
  }

  /* We cannot return simply "cvd" as we do not want duplication in the group
   * name across the instance groups owned by the user. Note that the set of ids
   * are expected to be unique to the user, so we use the ids. If ever the end
   * user happened to have already used the generated name, we did our best, and
   * cvd start will fail with a proper error message.
   */
  auto unique_suffix =
      std::to_string(*std::min_element(ids.begin(), ids.end()));
  return base_name + "_" + unique_suffix;
}

Result<std::string> CreationAnalyzer::AnalyzeHome() const {
  auto system_wide_home = CF_EXPECT(SystemWideUserHome(credential_.uid));
  if (Contains(envs_, "HOME") && envs_.at("HOME") != system_wide_home) {
    return envs_.at("HOME");
  }

  if (selector_options_parser_.IsMaybeDefaultGroup()) {
    auto groups = CF_EXPECT(
        instance_database_.FindGroups({kHomeField, system_wide_home}));
    if (groups.empty()) {
      return system_wide_home;
    }
  }

  CF_EXPECT(!group_name_.empty(),
            "To auto-generate HOME, the group name is a must.");
  const auto client_uid = credential_.uid;
  const auto client_gid = credential_.gid;
  std::string auto_generated_home =
      CF_EXPECT(ParentOfAutogeneratedHomes(client_uid, client_gid));
  auto_generated_home.append("/" + std::to_string(client_uid));
  auto_generated_home.append("/" + group_name_);
  CF_EXPECT(EnsureDirectoryExistsAllTheWay(auto_generated_home));
  return auto_generated_home;
}

}  // namespace selector
}  // namespace cuttlefish
