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

#include <optional>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/scopeguard.h>
#include <fmt/format.h>

#include "common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {
namespace selector {

namespace {

constexpr const char kJsonGroups[] = "Groups";

constexpr const unsigned UNSET_ID = 0;

Result<std::string> GenUniqueGroupName(const cvd::PersistentData& data) {
  std::set<std::string> group_names;
  for (const auto& group : data.instance_groups()) {
    group_names.insert(group.name());
  }
  for (size_t i = 1; i <= group_names.size() + 1; ++i) {
    auto name = fmt::format("{}_{}", kInternalGroupName, i);
    if (!Contains(group_names, name)) {
      return name;
    }
  }
  return CF_ERRF(
      "Can't generate unique group name: Somehow a set of size {} "
      "contains {} elements",
      group_names.size(), group_names.size() + 1);
}


}  // namespace

InstanceDatabase::InstanceDatabase(const std::string& backing_file)
    : viewer_(backing_file) {}

Result<bool> InstanceDatabase::IsEmpty() const {
  return viewer_.WithSharedLock<bool>([](const cvd::PersistentData& data) {
    return data.instance_groups().empty();
  });
}

Result<std::vector<LocalInstanceGroup>> InstanceDatabase::Clear() {
  return viewer_.WithExclusiveLock<std::vector<LocalInstanceGroup>>(
      [](cvd::PersistentData& data) -> Result<std::vector<LocalInstanceGroup>> {
        auto copy = data.instance_groups();
        data.clear_instance_groups();
        std::vector<LocalInstanceGroup> groups;
        for (const auto& group_proto : copy) {
          groups.push_back(CF_EXPECT(LocalInstanceGroup::Create(group_proto)));
        }
        return groups;
      });
}

Result<LocalInstanceGroup> InstanceDatabase::AddInstanceGroup(
    cvd::InstanceGroup& group_proto) {
  CF_EXPECTF(group_proto.name().empty() || IsValidGroupName(group_proto.name()),
             "GroupName \"{}\" is ill-formed.", group_proto.name());
  for (const auto& instance_proto : group_proto.instances()) {
    CF_EXPECTF(IsValidInstanceName(instance_proto.name()),
               "instance_name \"{}\" is invalid", instance_proto.name());
  }
  auto add_res = viewer_.WithExclusiveLock<LocalInstanceGroup>(
      [&group_proto](cvd::PersistentData& data) -> Result<LocalInstanceGroup> {
        if (group_proto.name().empty()) {
          group_proto.set_name(CF_EXPECT(GenUniqueGroupName(data)));
        }
        CF_EXPECTF(EnsureDirectoryExists(group_proto.home_directory()),
                   "HOME dir, \"{}\" neither exists nor can be created.",
                   group_proto.home_directory());
        auto matching_groups =
            FindGroups(data, {.home = group_proto.home_directory(),
                              .group_name = group_proto.name()});
        CF_EXPECTF(matching_groups.empty(),
                   "New group conflicts with existing group: {} at {}",
                   matching_groups[0].GroupName(),
                   matching_groups[0].HomeDir());
        for (const auto& instance_proto : group_proto.instances()) {
          if (instance_proto.id() == UNSET_ID) {
            continue;
          }
          auto matching_instances =
              FindInstances(data, {.id = instance_proto.id()});
          CF_EXPECTF(
              matching_instances.empty(),
              "New instance conflicts with existing instance: {} with id {}",
              matching_instances[0].name(),
              matching_instances[0].id());
        }
        auto new_group_proto = data.add_instance_groups();
        *new_group_proto = group_proto;
        return CF_EXPECT(LocalInstanceGroup::Create(*new_group_proto));
      });
  return CF_EXPECT(std::move(add_res));
}

Result<void> InstanceDatabase::UpdateInstanceGroup(
    const LocalInstanceGroup& group) {
  auto add_res = viewer_.WithExclusiveLock<void>(
      [&group](cvd::PersistentData& data) -> Result<void> {
        for (auto& group_proto : *data.mutable_instance_groups()) {
          if (group_proto.name() != group.GroupName()) {
            continue;
          }
          group_proto = group.Proto();
          // Instance protos may have been updated too
          group_proto.clear_instances();
          for (const auto& instance : group.Instances()) {
            *group_proto.add_instances() = instance;
          }
          return {};
        }
        return CF_ERRF("Group not found (name = {})", group.GroupName());
      });
  CF_EXPECT(std::move(add_res));
  return {};
}

Result<void> InstanceDatabase::UpdateInstance(const LocalInstanceGroup& group,
                                              const cvd::Instance& instance) {
  auto add_res = viewer_.WithExclusiveLock<void>(
      [&instance, &group](cvd::PersistentData& data) -> Result<void> {
        for (auto& group_proto : *data.mutable_instance_groups()) {
          if (group_proto.name() != group.Proto().name()) {
            continue;
          }
          for (auto& instance_proto : *group_proto.mutable_instances()) {
              if (instance_proto.name() != instance.name()) {
              continue;
              }
              instance_proto = instance;
              return {};
          }
          return CF_ERRF("Instance not found (name = '{}', group = '{}')",
                         group.Proto().name(), instance.name());
        }
        return CF_ERRF("Group not found (name = {})", group.Proto().name());
      });
  CF_EXPECT(std::move(add_res));
  return {};
}

Result<bool> InstanceDatabase::RemoveInstanceGroup(
    const std::string& group_name) {
  return viewer_.WithExclusiveLock<bool>([&group_name](
                                             cvd::PersistentData& data) {
    auto mutable_groups = data.mutable_instance_groups();
    for (auto it = mutable_groups->begin(); it != mutable_groups->end(); ++it) {
      if (it->name() == group_name) {
        mutable_groups->erase(it);
        return true;
      }
    }
    return false;
  });
}

bool InstanceDatabase::FindParam::Matches(
    const cvd::InstanceGroup& group) const {
  return (!home || *home == group.home_directory()) &&
         (!group_name || *group_name == group.name());
}

bool InstanceDatabase::FindParam::Matches(const cvd::Instance& instance) const {
  return (!id || *id == instance.id()) &&
         (!instance_name || *instance_name == instance.name());
}

Result<InstanceDatabase::FindParam> InstanceDatabase::FindParam::FromQueries(
    const Queries& queries) {
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
      [&param](const cvd::PersistentData& data) {
        return FindGroups(data, param);
      });
}

Result<std::vector<cvd::Instance>> InstanceDatabase::FindInstances(
    FindParam param) const {
  return viewer_.WithSharedLock<std::vector<cvd::Instance>>(
      [&param](const cvd::PersistentData& data) {
        return FindInstances(data, param);
      });
}

std::vector<LocalInstanceGroup> InstanceDatabase::FindGroups(
    const cvd::PersistentData& data, FindParam param) {
  std::vector<LocalInstanceGroup> ret;
  for (const auto& group : data.instance_groups()) {
    if (!param.Matches(group)) {
      continue;
    }
    if (std::none_of(group.instances().begin(), group.instances().end(),
                     [&param](const auto& instance) {
                       return param.Matches(instance);
                     })) {
      continue;
    }
    auto group_res = LocalInstanceGroup::Create(group);
    CHECK(group_res.ok()) << "Instance group from database fails validation: "
                          << group_res.error().FormatForEnv();
    ret.push_back(*group_res);
  }
  return ret;
}

std::vector<cvd::Instance> InstanceDatabase::FindInstances(
    const cvd::PersistentData& data, FindParam param) {
  std::vector<cvd::Instance> ret;
  for (const auto& group : data.instance_groups()) {
    if (!param.Matches(group)) {
      continue;
    }
    for (const auto& instance : group.instances()) {
      if (!param.Matches(instance)) {
        continue;
      }
      ret.push_back(instance);
    }
  }
  return ret;
}

Result<std::pair<cvd::Instance, LocalInstanceGroup>>
InstanceDatabase::FindInstanceWithGroup(const Queries& queries) const {
  auto param = CF_EXPECT(FindParam::FromQueries(queries));
  return viewer_.WithSharedLock<std::pair<cvd::Instance, LocalInstanceGroup>>(
      [&param](const auto& data)
          -> Result<std::pair<cvd::Instance, LocalInstanceGroup>> {
        std::optional<std::pair<cvd::Instance, LocalInstanceGroup>> result_opt;
        for (const auto& group : data.instance_groups()) {
          if (!param.Matches(group)) {
            continue;
          }
          for (const auto& instance : group.instances()) {
            if (!param.Matches(instance)) {
              continue;
            }
            CF_EXPECT(!result_opt.has_value(), "Found more than one instance");
            result_opt = {instance,
                          CF_EXPECT(LocalInstanceGroup::Create(group))};
          }
        }
        return CF_EXPECT(std::move(result_opt), "Found no matches");
      });
}

Result<std::vector<LocalInstanceGroup>> InstanceDatabase::InstanceGroups()
    const {
  return viewer_.WithSharedLock<std::vector<LocalInstanceGroup>>(
      [](const auto& data) -> Result<std::vector<LocalInstanceGroup>> {
        std::vector<LocalInstanceGroup> ret;
        for (const auto& group_proto : data.instance_groups()) {
          ret.push_back(CF_EXPECT(LocalInstanceGroup::Create(group_proto)));
        }
        return ret;
      });
}

Result<void> InstanceDatabase::LoadFromJson(const Json::Value& db_json) {
  std::vector<LocalInstanceGroup> new_groups;
  CF_EXPECT(db_json.isMember(kJsonGroups));
  const Json::Value& group_array = db_json[kJsonGroups];
  CF_EXPECT(group_array.isArray());
  int n_groups = group_array.size();
  for (int i = 0; i < n_groups; i++) {
    new_groups.push_back(
        CF_EXPECT(LocalInstanceGroup::Deserialize(group_array[i])));
  }
  return viewer_.WithExclusiveLock<void>(
      [&new_groups](cvd::PersistentData& data) -> Result<void> {
        for (const auto& group : new_groups) {
          *data.add_instance_groups() = group.Proto();
        }
        return {};
      });
}

Result<void> InstanceDatabase::SetAcloudTranslatorOptout(bool optout) {
  return viewer_.WithExclusiveLock<void>(
      [optout](cvd::PersistentData& data) -> Result<void> {
        data.set_acloud_translator_optout(optout);
        return {};
      });
}

Result<bool> InstanceDatabase::GetAcloudTranslatorOptout() const {
  return viewer_.WithSharedLock<bool>(
      [](const cvd::PersistentData& data) -> Result<bool> {
        return data.acloud_translator_optout();
      });
}

}  // namespace selector
}  // namespace cuttlefish
