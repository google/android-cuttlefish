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

#include "host/commands/cvd/selector/instance_database.h"

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/scopeguard.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {
namespace selector {

std::vector<std::unique_ptr<LocalInstanceGroup>>::iterator
InstanceDatabase::FindIterator(const LocalInstanceGroup& group) {
  for (auto itr = local_instance_groups_.begin();
       itr != local_instance_groups_.end(); itr++) {
    if (itr->get() == std::addressof(group)) {
      return itr;
    }
  }
  // must not reach here
  return local_instance_groups_.end();
}

void InstanceDatabase::Clear() { local_instance_groups_.clear(); }

Result<ConstRef<LocalInstanceGroup>> InstanceDatabase::AddInstanceGroup(
    const AddInstanceGroupParam& param) {
  CF_EXPECTF(IsValidGroupName(param.group_name),
             "GroupName \"{}\" is ill-formed.", param.group_name);
  CF_EXPECTF(EnsureDirectoryExists(param.home_dir),
             "HOME dir, \"{}\" neither exists nor can be created.",
             param.home_dir);
  CF_EXPECTF(PotentiallyHostArtifactsPath(param.host_artifacts_path),
             "ANDROID_HOST_OUT, \"{}\" is not a tool directory",
             param.host_artifacts_path);
  std::vector<Query> queries = {{kHomeField, param.home_dir},
                                {kGroupNameField, param.group_name}};
  for (const auto& query : queries) {
    auto instance_groups =
        CF_EXPECT(Find<LocalInstanceGroup>(query, group_handlers_));
    CF_EXPECTF(instance_groups.empty(), "[\"{}\" : \"{}\"] is already taken",
               query.field_name_, query.field_value_);
  }
  auto new_group =
      new LocalInstanceGroup({.group_name = param.group_name,
                              .home_dir = param.home_dir,
                              .host_artifacts_path = param.host_artifacts_path,
                              .product_out_path = param.product_out_path});
  CF_EXPECT(new_group != nullptr);
  local_instance_groups_.emplace_back(new_group);
  const auto raw_ptr = local_instance_groups_.back().get();
  ConstRef<LocalInstanceGroup> const_ref = *raw_ptr;
  return {const_ref};
}

Result<void> InstanceDatabase::AddInstance(const std::string& group_name,
                                           const unsigned id,
                                           const std::string& instance_name) {
  LocalInstanceGroup* group_ptr = CF_EXPECT(FindMutableGroup(group_name));
  LocalInstanceGroup& group = *group_ptr;

  CF_EXPECTF(IsValidInstanceName(instance_name),
             "instance_name \"{}\" is invalid", instance_name);
  auto itr = FindIterator(group);
  CF_EXPECTF(itr != local_instance_groups_.end() && *itr != nullptr,
             "Adding instances to non-existing group \"{}\"",
             group.InternalGroupName());

  auto instances =
      CF_EXPECT(FindInstances({kInstanceIdField, std::to_string(id)}));
  CF_EXPECTF(instances.empty(), "instance id \"{}\" is taken.", id);

  auto instances_by_name = CF_EXPECT((*itr)->FindByInstanceName(instance_name));
  CF_EXPECTF(instances_by_name.empty(),
             "instance name \"{}\" is already taken.", instance_name);
  return (*itr)->AddInstance(id, instance_name);
}

Result<void> InstanceDatabase::AddInstances(
    const std::string& group_name, const std::vector<InstanceInfo>& instances) {
  for (const auto& instance_info : instances) {
    CF_EXPECT(AddInstance(group_name, instance_info.id, instance_info.name));
  }
  return {};
}

Result<void> InstanceDatabase::SetBuildId(const std::string& group_name,
                                          const std::string& build_id) {
  auto* group_ptr = CF_EXPECT(FindMutableGroup(group_name));
  auto& group = *group_ptr;
  group.SetBuildId(build_id);
  return {};
}

Result<LocalInstanceGroup*> InstanceDatabase::FindMutableGroup(
    const std::string& group_name) {
  LocalInstanceGroup* group_ptr = nullptr;
  for (auto& group_uniq_ptr : local_instance_groups_) {
    if (group_uniq_ptr && group_uniq_ptr->GroupName() == group_name) {
      group_ptr = group_uniq_ptr.get();
      break;
    }
  }
  CF_EXPECTF(group_ptr != nullptr,
             "Instance Group named as \"{}\" is not found.", group_name);
  return group_ptr;
}

bool InstanceDatabase::RemoveInstanceGroup(const std::string& group_name) {
  auto group_result = FindGroup({kGroupNameField, group_name});
  if (!group_result.ok()) {
    return false;
  }
  const LocalInstanceGroup& group = group_result->Get();
  return RemoveInstanceGroup(group);
}

bool InstanceDatabase::RemoveInstanceGroup(const LocalInstanceGroup& group) {
  auto itr = FindIterator(group);
  // *itr is the reference to the unique pointer object
  if (itr == local_instance_groups_.end() || !(*itr)) {
    return false;
  }
  local_instance_groups_.erase(itr);
  return true;
}

Result<Set<ConstRef<LocalInstanceGroup>>> InstanceDatabase::FindGroupsByHome(
    const std::string& home) const {
  auto subset = CollectToSet<LocalInstanceGroup>(
      local_instance_groups_,
      [&home](const std::unique_ptr<LocalInstanceGroup>& group) {
        if (!group) {
          return false;
        }
        if (group->HomeDir() == home) {
          return true;
        }
        if (group->HomeDir().empty() || home.empty()) {
          return false;
        }
        // The two paths must be an absolute path.
        // this is guaranteed by the CreationAnalyzer
        std::string home_realpath;
        std::string group_home_realpath;
        if (!android::base::Realpath(home, std::addressof(home_realpath))) {
          return false;
        }
        if (!android::base::Realpath(group->HomeDir(),
                                     std::addressof(group_home_realpath))) {
          return false;
        }
        return home_realpath == group_home_realpath;
      });
  return AtMostOne(subset, GenerateTooManyInstancesErrorMsg(1, kHomeField));
}

Result<Set<ConstRef<LocalInstanceGroup>>>
InstanceDatabase::FindGroupsByGroupName(const std::string& group_name) const {
  auto subset = CollectToSet<LocalInstanceGroup>(
      local_instance_groups_,
      [&group_name](const std::unique_ptr<LocalInstanceGroup>& group) {
        return (group && group->GroupName() == group_name);
      });
  return AtMostOne(subset,
                   GenerateTooManyInstancesErrorMsg(1, kGroupNameField));
}

Result<Set<ConstRef<LocalInstanceGroup>>>
InstanceDatabase::FindGroupsByInstanceName(
    const std::string& instance_name) const {
  auto subset = CollectToSet<LocalInstanceGroup>(
      local_instance_groups_,
      [&instance_name](const std::unique_ptr<LocalInstanceGroup>& group) {
        if (!group) {
          return false;
        }
        auto instance_set_result = group->FindByInstanceName(instance_name);
        return instance_set_result.ok() && (instance_set_result->size() == 1);
      });
  return subset;
}

Result<Set<ConstRef<LocalInstance>>> InstanceDatabase::FindInstancesByHome(
    const std::string& home) const {
  auto collector =
      [&home](const std::unique_ptr<LocalInstanceGroup>& group)
      -> Result<Set<ConstRef<LocalInstance>>> {
    CF_EXPECT(group != nullptr);
    CF_EXPECTF(group->HomeDir() == home,
               "Group Home, \"{}\", is different from the input home query "
               "\"{}\"",
               group->HomeDir(), home);
    return (group->FindAllInstances());
  };
  return CollectAllElements<LocalInstance, LocalInstanceGroup>(
      collector, local_instance_groups_);
}

Result<Set<ConstRef<LocalInstance>>> InstanceDatabase::FindInstancesById(
    const std::string& id) const {
  int parsed_int = 0;
  CF_EXPECTF(android::base::ParseInt(id, &parsed_int),
             "\"{}\" cannot be converted to an integer.", id);
  auto collector =
      [parsed_int](const std::unique_ptr<LocalInstanceGroup>& group)
      -> Result<Set<ConstRef<LocalInstance>>> {
    CF_EXPECT(group != nullptr);
    return group->FindById(parsed_int);
  };
  auto subset = CollectAllElements<LocalInstance, LocalInstanceGroup>(
      collector, local_instance_groups_);
  CF_EXPECT(subset.ok());
  return AtMostOne(*subset,
                   GenerateTooManyInstancesErrorMsg(1, kInstanceIdField));
}

Result<Set<ConstRef<LocalInstance>>>
InstanceDatabase::FindInstancesByInstanceName(
    const Value& instance_specific_name) const {
  auto collector = [&instance_specific_name](
                       const std::unique_ptr<LocalInstanceGroup>& group)
      -> Result<Set<ConstRef<LocalInstance>>> {
    CF_EXPECT(group != nullptr);
    return (group->FindByInstanceName(instance_specific_name));
  };
  return CollectAllElements<LocalInstance, LocalInstanceGroup>(
      collector, local_instance_groups_);
}

Result<Set<ConstRef<LocalInstance>>> InstanceDatabase::FindInstancesByGroupName(
    const Value& group_name) const {
  auto collector =
      [&group_name](const std::unique_ptr<LocalInstanceGroup>& group)
      -> Result<Set<ConstRef<LocalInstance>>> {
    CF_EXPECT(group != nullptr);
    if (group->GroupName() != group_name) {
      Set<ConstRef<LocalInstance>> empty_set;
      return empty_set;
    }
    return (group->FindAllInstances());
  };
  return CollectAllElements<LocalInstance, LocalInstanceGroup>(
      collector, local_instance_groups_);
}

Json::Value InstanceDatabase::Serialize() const {
  Json::Value instance_db_json;
  int i = 0;
  Json::Value group_array;
  for (const auto& local_instance_group : local_instance_groups_) {
    group_array[i] = local_instance_group->Serialize();
    ++i;
  }
  instance_db_json[kJsonGroups] = group_array;
  return instance_db_json;
}

Result<void> InstanceDatabase::LoadGroupFromJson(
    const Json::Value& group_json) {
  const std::string group_name =
      group_json[LocalInstanceGroup::kJsonGroupName].asString();
  const std::string home_dir =
      group_json[LocalInstanceGroup::kJsonHomeDir].asString();
  const std::string host_artifacts_path =
      group_json[LocalInstanceGroup::kJsonHostArtifactPath].asString();
  const std::string product_out_path =
      group_json[LocalInstanceGroup::kJsonProductOutPath].asString();
  const std::string build_id_value =
      group_json[LocalInstanceGroup::kJsonBuildId].asString();
  std::optional<std::string> build_id;
  if (build_id_value != LocalInstanceGroup::kJsonUnknownBuildId) {
    build_id = build_id_value;
  }
  const auto new_group_ref =
      CF_EXPECT(AddInstanceGroup({.group_name = group_name,
                                  .home_dir = home_dir,
                                  .host_artifacts_path = host_artifacts_path,
                                  .product_out_path = product_out_path}));
  if (build_id) {
    CF_EXPECT(SetBuildId(group_name, *build_id));
  }
  android::base::ScopeGuard remove_already_added_new_group(
      [&new_group_ref, this]() {
        this->RemoveInstanceGroup(new_group_ref.Get());
      });
  const Json::Value& instances_json_array =
      group_json[LocalInstanceGroup::kJsonInstances];
  for (int i = 0; i < instances_json_array.size(); i++) {
    const Json::Value& instance_json = instances_json_array[i];
    const std::string instance_name =
        instance_json[LocalInstance::kJsonInstanceName].asString();
    const std::string instance_id =
        instance_json[LocalInstance::kJsonInstanceId].asString();

    int id;
    auto parse_result =
        android::base::ParseInt(instance_id, std::addressof(id));
    CF_EXPECTF(parse_result == true, "Invalid instance ID in instance json: {}",
               instance_id);
    CF_EXPECTF(AddInstance(group_name, id, instance_name),
               "Adding instance [{} : \"{}\"] to the group \"{}\" failed.",
               instance_name, id, group_name);
  }
  remove_already_added_new_group.Disable();
  return {};
}

Result<void> InstanceDatabase::LoadFromJson(const Json::Value& db_json) {
  const Json::Value& group_array = db_json[kJsonGroups];
  int n_groups = group_array.size();
  for (int i = 0; i < n_groups; i++) {
    CF_EXPECT(LoadGroupFromJson(group_array[i]));
  }
  return {};
}

}  // namespace selector
}  // namespace cuttlefish
