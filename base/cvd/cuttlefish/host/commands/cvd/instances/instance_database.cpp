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

#include "host/commands/cvd/instances/instance_database.h"

#include <optional>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/scopeguard.h>
#include <fmt/format.h>

#include "common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "host/commands/cvd/instances/instance_database_utils.h"
#include "host/commands/cvd/instances/instance_group_record.h"
#include "host/commands/cvd/instances/instance_record.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {

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

// Whether the instance fields in the filter match the given instance. It
// doesn't check whether the group the instance belongs to also matches the
// filter as it's assumed that was checked before.
bool InstanceMatches(const cvd::Instance& instance,
                     const InstanceDatabase::Filter& filter) {
  return (!filter.instance_id || *filter.instance_id == instance.id()) &&
         (filter.instance_names.empty() ||
          Contains(filter.instance_names, instance.name()));
}

// Whether the filter matches a given group, including whether it contains
// instances matching the instance related fields.
bool GroupMatches(const cvd::InstanceGroup& group,
                  const InstanceDatabase::Filter& filter) {
  if (filter.home && filter.home != group.home_directory()) {
    return false;
  }
  if (filter.group_name && filter.group_name != group.name()) {
    return false;
  }
  std::unordered_set<unsigned> instance_ids;
  std::unordered_set<std::string> instance_names;
  for (const auto& instance : group.instances()) {
    instance_ids.insert(instance.id());
    instance_names.insert(instance.name());
  }
  if (filter.instance_id && !Contains(instance_ids, *filter.instance_id)) {
    return false;
  }
  for (const auto& instance_name : filter.instance_names) {
    if (!Contains(instance_names, instance_name)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool InstanceDatabase::Filter::Empty() const {
  return !home && !instance_id && !group_name && instance_names.empty();
}

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
        CF_EXPECTF(FindGroups(data, {.group_name = group_proto.name()}).empty(),
                   "An instance group already exists with name: {}",
                   group_proto.name());
        CF_EXPECTF(
            FindGroups(data, {.home = group_proto.home_directory()}).empty(),
            "An instance group already exists with HOME directory: {}",
            group_proto.home_directory());
        CF_EXPECTF(EnsureDirectoryExists(group_proto.home_directory()),
                   "HOME dir, \"{}\" neither exists nor can be created.",
                   group_proto.home_directory());
        std::unordered_map<uint32_t, std::string> ids_to_name_map;
        for (const auto& group : data.instance_groups()) {
          for (const auto& instance : group.instances()) {
            if (instance.id() != UNSET_ID) {
              ids_to_name_map[instance.id()] =
                  fmt::format("{}/{}", group.name(), instance.name());
            }
          }
        }
        for (const auto& instance_proto : group_proto.instances()) {
          if (instance_proto.id() == UNSET_ID) {
            continue;
          }
          auto find_it = ids_to_name_map.find(instance_proto.id());
          CF_EXPECTF(
              find_it == ids_to_name_map.end(),
              "New instance conflicts with existing instance: {} with id {}",
              find_it->second, find_it->first);
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
          return {};
        }
        return CF_ERRF("Group not found (name = {})", group.GroupName());
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

Result<std::vector<LocalInstanceGroup>> InstanceDatabase::FindGroups(
    const Filter& filter) const {
  return viewer_.WithSharedLock<std::vector<LocalInstanceGroup>>(
      [&filter](const cvd::PersistentData& data) {
        return FindGroups(data, filter);
      });
}

std::vector<LocalInstanceGroup> InstanceDatabase::FindGroups(
    const cvd::PersistentData& data, const Filter& filter) {
  std::vector<LocalInstanceGroup> ret;
  for (const auto& group : data.instance_groups()) {
    if (!GroupMatches(group, filter)) {
      continue;
    }
    auto group_res = LocalInstanceGroup::Create(group);
    CHECK(group_res.ok()) << "Instance group from database fails validation: "
                          << group_res.error().FormatForEnv();
    ret.push_back(*group_res);
  }
  return ret;
}

Result<std::pair<LocalInstance, LocalInstanceGroup>>
InstanceDatabase::FindInstanceWithGroup(const Filter& filter) const {
  CF_EXPECT_LE(filter.instance_names.size(), 1u,
               "Can't find single instance when multiple names specified: "
                   << filter.instance_names.size());
  return viewer_.WithSharedLock<std::pair<LocalInstance, LocalInstanceGroup>>(
      [&filter](const auto& data)
          -> Result<std::pair<LocalInstance, LocalInstanceGroup>> {
        std::optional<std::pair<LocalInstance, LocalInstanceGroup>> result_opt;
        for (const auto& group : data.instance_groups()) {
          if (!GroupMatches(group, filter)) {
            continue;
          }
          for (int i = 0; i < group.instances_size(); ++i) {
            const auto& instance = group.instances(i);
            if (!InstanceMatches(instance, filter)) {
              continue;
            }
            CF_EXPECT(!result_opt.has_value(), "Found more than one instance");
            LocalInstanceGroup local_group =
                CF_EXPECT(LocalInstanceGroup::Create(group));
            result_opt =
                std::make_pair(local_group.Instances()[i], local_group);
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

}  // namespace cuttlefish
