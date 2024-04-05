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

#include <numeric>  // std::iota
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/scopeguard.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {
namespace selector {

namespace {
constexpr const char kJsonGroups[] = "Groups";

}

std::string SerializeGroups(const std::vector<LocalInstanceGroup>& groups) {
  Json::Value instance_db_json;
  Json::Value group_array(Json::ValueType::arrayValue);
  for (const auto& group : groups) {
    group_array.append(group.Serialize());
  }
  instance_db_json[kJsonGroups] = group_array;
  return instance_db_json.toStyledString();
}

Result<std::vector<LocalInstanceGroup>> DeserializeGroups(
    const std::string& str) {
  if (str.empty()) {
    // The backing file was empty or didn't exist.
    return {};
  }
  std::vector<LocalInstanceGroup> ret;
  auto json_db = CF_EXPECT(ParseJson(str), "Error parsing JSON");
  if (!json_db.isMember(kJsonGroups) || json_db[kJsonGroups].isNull()) {
    // Older cvd version may write null to this field when the db is empty.
    return ret;
  }
  auto group_array = json_db[kJsonGroups];
  CF_EXPECTF(group_array.isArray(), "Expected '{}' property to be an array",
             kJsonGroups);
  int n_groups = group_array.size();
  for (int i = 0; i < n_groups; i++) {
    auto group = CF_EXPECT(LocalInstanceGroup::Deserialize(group_array[i]));
    ret.push_back(group);
  }
  return ret;
}

InstanceDatabase::InstanceDatabase(const std::string& backing_file)
    : viewer_(backing_file, SerializeGroups, DeserializeGroups) {}

Result<bool> InstanceDatabase::IsEmpty() const {
  return viewer_.WithSharedLock<bool>(
      [](const std::vector<LocalInstanceGroup>& groups) {
        return groups.empty();
      });
}

Result<void> InstanceDatabase::Clear() {
  return viewer_.WithExclusiveLock<void>(
      [](std::vector<LocalInstanceGroup>& groups) -> Result<void> {
        groups.clear();
        return {};
      });
}

Result<void> InstanceDatabase::AddInstanceGroup(const InstanceGroup& param) {
  CF_EXPECTF(IsValidGroupName(param.group_name),
             "GroupName \"{}\" is ill-formed.", param.group_name);
  CF_EXPECTF(EnsureDirectoryExists(param.home_dir),
             "HOME dir, \"{}\" neither exists nor can be created.",
             param.home_dir);
  CF_EXPECTF(PotentiallyHostArtifactsPath(param.host_artifacts_path),
             "ANDROID_HOST_OUT, \"{}\" is not a tool directory",
             param.host_artifacts_path);
  return viewer_.WithExclusiveLock<void>(
      [&param](std::vector<LocalInstanceGroup>& groups) -> Result<void> {
        for (const auto& group : groups) {
          CF_EXPECTF(group.HomeDir() != param.home_dir,
                     "A group already exist in dir: {}", param.home_dir);
          CF_EXPECTF(group.GroupName() != param.group_name,
                     "A group named '{}' already exists", param.group_name);
        }
        groups.emplace_back(param);
        return {};
      });
}

Result<void> InstanceDatabase::AddInstance(const std::string& group_name,
                                           const unsigned id,
                                           const std::string& instance_name) {
  return AddInstances(group_name, {{.id = id, .name = instance_name}});
}

Result<void> InstanceDatabase::AddInstances(
    const std::string& group_name, const std::vector<InstanceInfo>& instances) {
  for (const auto& instance_info : instances) {
    CF_EXPECTF(IsValidInstanceName(instance_info.name),
               "instance_name \"{}\" is invalid", instance_info.name);

    auto instances = CF_EXPECT(FindInstances(
        Query{kInstanceIdField, std::to_string(instance_info.id)}));
    CF_EXPECTF(instances.empty(), "instance id \"{}\" is taken.",
               instance_info.id);
  }
  return viewer_.WithExclusiveLock<void>(
      [&group_name,
       &instances](std::vector<LocalInstanceGroup>& groups) -> Result<void> {
        for (auto& group : groups) {
          if (group.GroupName() != group_name) {
            continue;
          }
          for (const auto& instance : instances) {
            auto instances_by_name =
                CF_EXPECT(group.FindByInstanceName(instance.name));
            CF_EXPECTF(instances_by_name.empty(),
                       "instance name \"{}\" is already taken.", instance.name);
            return group.AddInstance(instance.id, instance.name);
          }
        }
        return CF_ERRF("Group not found: {}", group_name);
      });
}

Result<bool> InstanceDatabase::RemoveInstanceGroup(
    const std::string& group_name) {
  return viewer_.WithExclusiveLock<bool>(
      [&group_name](std::vector<LocalInstanceGroup>& groups) {
        auto pred = [&group_name](const auto& group) {
          return group.GroupName() == group_name;
        };
        auto it = std::remove_if(groups.begin(), groups.end(), pred);
        bool removed_any = it != groups.end();
        groups.erase(it, groups.end());
        return removed_any;
      });
}

Result<InstanceDatabase::FindParam> InstanceDatabase::ParamFromQueries(
    const Queries& queries) const {
  FindParam param;
  for (const auto& query : queries) {
    if (query.field_name_ == kHomeField) {
      param.home = query.field_value_;
    } else if (query.field_name_ == kInstanceIdField) {
      int id;
      CF_EXPECTF(android::base::ParseInt(query.field_value_, &id),
                 "Id is not a number: {}", id);
      param.id = id;
    } else if (query.field_name_ == kGroupNameField) {
      param.group_name = query.field_value_;
    } else if (query.field_name_ == kInstanceNameField) {
      param.instance_name = query.field_value_;
    } else {
      return CF_ERRF("Unrecognized field name: {}", query.field_name_);
    }
  }
  return param;
}

Result<std::vector<LocalInstanceGroup>> InstanceDatabase::FindGroups(
    FindParam param) const {
  return viewer_.WithSharedLock<std::vector<LocalInstanceGroup>>(
      [&param](const std::vector<LocalInstanceGroup>& groups)
          -> Result<std::vector<LocalInstanceGroup>> {
        std::vector<LocalInstanceGroup> ret;
        for (const auto& group : groups) {
          if (param.home && param.home != group.HomeDir()) {
            continue;
          }
          if (param.group_name && param.group_name != group.GroupName()) {
            continue;
          }
          if (param.id) {
            if (CF_EXPECT(group.FindById(*param.id)).empty()) {
              continue;
            }
          }
          if (param.instance_name &&
              CF_EXPECT(group.FindByInstanceName(*param.instance_name))
                  .empty()) {
            continue;
          }
          ret.push_back(group);
        }
        return ret;
      });
}

Result<std::vector<LocalInstance>> InstanceDatabase::FindInstances(
    FindParam param) const {
  return viewer_.WithSharedLock<std::vector<LocalInstance>>(
      [&param](const std::vector<LocalInstanceGroup>& groups) {
        std::vector<LocalInstance> ret;
        for (const auto& group : groups) {
          if (param.group_name && param.group_name != group.GroupName()) {
            continue;
          }
          if (param.home && param.home != group.HomeDir()) {
            continue;
          }
          for (const auto& instance : group.Instances()) {
            if (param.id && param.id != instance.InstanceId()) {
              continue;
            }
            if (param.instance_name &&
                param.instance_name != instance.PerInstanceName()) {
              continue;
            }
            ret.push_back(instance);
          }
        }
        return ret;
      });
}

Result<std::vector<LocalInstanceGroup>> InstanceDatabase::InstanceGroups()
    const {
  return viewer_.WithSharedLock<std::vector<LocalInstanceGroup>>(
      [](const auto& groups) { return groups; });
}

Result<Json::Value> InstanceDatabase::Serialize() const {
  return viewer_.WithSharedLock<Json::Value>(
      [](const std::vector<LocalInstanceGroup>& groups) {
        Json::Value instance_db_json;
        Json::Value group_array;
        for (const auto& group : groups) {
          group_array.append(group.Serialize());
        }
        instance_db_json[kJsonGroups] = group_array;
        return instance_db_json;
      });
}

Result<void> InstanceDatabase::LoadFromJson(const Json::Value& db_json) {
  std::vector<LocalInstanceGroup> new_groups;
  const Json::Value& group_array = db_json[kJsonGroups];
  int n_groups = group_array.size();
  for (int i = 0; i < n_groups; i++) {
    new_groups.push_back(
        CF_EXPECT(LocalInstanceGroup::Deserialize(group_array[i])));
  }
  return viewer_.WithExclusiveLock<void>(
      [&new_groups](std::vector<LocalInstanceGroup>& groups) -> Result<void> {
        groups = std::move(new_groups);
        return {};
      });
}

}  // namespace selector
}  // namespace cuttlefish
