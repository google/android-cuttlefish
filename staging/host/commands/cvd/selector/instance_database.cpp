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
