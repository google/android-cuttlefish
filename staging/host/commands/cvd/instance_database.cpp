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

#include "host/commands/cvd/instance_database.h"

#include "host/commands/cvd/selector_constants.h"

namespace cuttlefish {
namespace instance_db {

InstanceDatabase::InstanceDatabase() {
  group_handlers_[selector::kHomeField] = [this](const Value& field_value) {
    return FindGroupsByHome(field_value);
  };
  instance_handlers_[selector::kInstanceIdField] =
      [this](const Value& field_value) {
        return FindInstancesById(field_value);
      };
}

bool InstanceDatabase::IsEmpty() const {
  return local_instance_groups_.empty();
}

template <typename T>
Result<Set<T>> InstanceDatabase::Find(
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
Result<T> InstanceDatabase::FindOne(
    const Query& query,
    const Map<FieldName, ConstHandler<T>>& handler_map) const {
  auto set = CF_EXPECT(Find<T>(query, handler_map));
  if (set.size() != 1) {
    return CF_ERR("Only one Instance (Group) is expected but "
                  << set.size() << " was found.");
  }
  return {*set.cbegin()};
}

Result<Set<LocalInstanceGroup>> InstanceDatabase::FindGroups(
    const Query& query) const {
  return Find<LocalInstanceGroup>(query, group_handlers_);
}

Result<Set<LocalInstance>> InstanceDatabase::FindInstances(
    const Query& query) const {
  return Find<LocalInstance>(query, instance_handlers_);
}

Result<LocalInstanceGroup> InstanceDatabase::FindGroup(
    const Query& query) const {
  return FindOne<LocalInstanceGroup>(query, group_handlers_);
}

Result<LocalInstance> InstanceDatabase::FindInstance(const Query& query) const {
  return FindOne<LocalInstance>(query, instance_handlers_);
}

}  // namespace instance_db
}  // namespace cuttlefish
