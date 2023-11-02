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

#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {
namespace selector {

InstanceDatabase::InstanceDatabase() {
  group_handlers_[kHomeField] = [this](const Value& field_value) {
    return FindGroupsByHome(field_value);
  };
  group_handlers_[kInstanceIdField] = [this](const Value& field_value) {
    return FindGroupsById(field_value);
  };
  group_handlers_[kGroupNameField] = [this](const Value& field_value) {
    return FindGroupsByGroupName(field_value);
  };
  group_handlers_[kInstanceNameField] = [this](const Value& field_value) {
    return FindGroupsByInstanceName(field_value);
  };
  instance_handlers_[kHomeField] = [this](const Value& field_value) {
    return FindInstancesByHome(field_value);
  };
  instance_handlers_[kInstanceIdField] = [this](const Value& field_value) {
    return FindInstancesById(field_value);
  };
  instance_handlers_[kGroupNameField] = [this](const Value& field_value) {
    return FindInstancesByGroupName(field_value);
  };
  instance_handlers_[kInstanceNameField] = [this](const Value& field_value) {
    return FindInstancesByInstanceName(field_value);
  };
}

bool InstanceDatabase::IsEmpty() const {
  return local_instance_groups_.empty();
}

template <typename T>
Result<Set<ConstRef<T>>> InstanceDatabase::Find(
    const Query& query,
    const Map<FieldName, ConstHandler<T>>& handler_map) const {
  static_assert(std::is_same<T, LocalInstance>::value ||
                std::is_same<T, LocalInstanceGroup>::value);
  const auto& [key, value] = query;
  auto itr = handler_map.find(key);
  if (itr == handler_map.end()) {
    return CF_ERR("Handler does not exist for query " << key);
  }
  return (itr->second)(value);
}

template <typename T>
Result<Set<ConstRef<T>>> InstanceDatabase::Find(
    const Queries& queries,
    const Map<FieldName, ConstHandler<T>>& handler_map) const {
  static_assert(std::is_same<T, LocalInstance>::value ||
                std::is_same<T, LocalInstanceGroup>::value);
  if (queries.empty()) {
    return CF_ERR("Queries must not be empty");
  }
  auto first_set = CF_EXPECT(Find<T>(queries[0], handler_map));
  for (int i = 1; i < queries.size(); i++) {
    auto subset = CF_EXPECT(Find<T>(queries[i], handler_map));
    first_set = Intersection(first_set, subset);
  }
  return {first_set};
}

template <typename T>
Result<ConstRef<T>> InstanceDatabase::FindOne(
    const Query& query,
    const Map<FieldName, ConstHandler<T>>& handler_map) const {
  auto set = CF_EXPECT(Find<T>(query, handler_map));
  CF_EXPECT_EQ(set.size(), 1, "Only one Instance (Group) is allowed.");
  return {*set.cbegin()};
}

template <typename T>
Result<ConstRef<T>> InstanceDatabase::FindOne(
    const Queries& queries,
    const Map<FieldName, ConstHandler<T>>& handler_map) const {
  auto set = CF_EXPECT(Find<T>(queries, handler_map));
  CF_EXPECT_EQ(set.size(), 1, "Only one Instance (Group) is allowed.");
  return {*set.cbegin()};
}

Result<Set<ConstRef<LocalInstanceGroup>>> InstanceDatabase::FindGroups(
    const Query& query) const {
  return Find<LocalInstanceGroup>(query, group_handlers_);
}

Result<Set<ConstRef<LocalInstanceGroup>>> InstanceDatabase::FindGroups(
    const Queries& queries) const {
  return Find<LocalInstanceGroup>(queries, group_handlers_);
}

Result<Set<ConstRef<LocalInstance>>> InstanceDatabase::FindInstances(
    const Query& query) const {
  return Find<LocalInstance>(query, instance_handlers_);
}

Result<Set<ConstRef<LocalInstance>>> InstanceDatabase::FindInstances(
    const Queries& queries) const {
  return Find<LocalInstance>(queries, instance_handlers_);
}

Result<ConstRef<LocalInstanceGroup>> InstanceDatabase::FindGroup(
    const Query& query) const {
  return FindOne<LocalInstanceGroup>(query, group_handlers_);
}

Result<ConstRef<LocalInstanceGroup>> InstanceDatabase::FindGroup(
    const Queries& queries) const {
  return FindOne<LocalInstanceGroup>(queries, group_handlers_);
}

Result<ConstRef<LocalInstance>> InstanceDatabase::FindInstance(
    const Query& query) const {
  return FindOne<LocalInstance>(query, instance_handlers_);
}

Result<ConstRef<LocalInstance>> InstanceDatabase::FindInstance(
    const Queries& queries) const {
  return FindOne<LocalInstance>(queries, instance_handlers_);
}

}  // namespace selector
}  // namespace cuttlefish
