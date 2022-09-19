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

#pragma once

#include <functional>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/instance_database_types.h"
#include "host/commands/cvd/instance_group_record.h"
#include "host/commands/cvd/instance_record.h"

namespace cuttlefish {
namespace instance_db {

// TODO(kwstephenkim): make this per-user instance database
class InstanceDatabase {
  template <typename T>
  using ConstHandler = std::function<Result<Set<T>>(const Value&)>;

  using ConstGroupHandler = ConstHandler<LocalInstanceGroup>;
  using ConstInstanceHandler = ConstHandler<LocalInstance>;

 public:
  InstanceDatabase();
  bool IsEmpty() const;
  Result<Set<LocalInstanceGroup>> FindGroups(const Query& query) const;
  Result<Set<LocalInstance>> FindInstances(const Query& query) const;
  const auto& InstanceGroups() const { return local_instance_groups_; }

 private:
  template <typename T>
  Result<Set<T>> Find(const Query& query,
                      const Map<FieldName, ConstHandler<T>>& handler_map) const;

  // actual Find implementations
  Result<Set<LocalInstanceGroup>> FindGroupsByHome(const Value& home) const;
  Result<Set<LocalInstance>> FindInstancesById(const Value& id) const;

  std::vector<LocalInstanceGroup> local_instance_groups_;
  Map<FieldName, ConstGroupHandler> group_handlers_;
  Map<FieldName, ConstInstanceHandler> instance_handlers_;
};

}  // namespace instance_db
}  // namespace cuttlefish
