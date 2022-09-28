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

  // returns CF_ERR if the home_directory is already taken
  Result<void> AddInstanceGroup(const std::string& home_dir,
                                const std::string& host_binaries_dir);

  /** Finds an InstanceGroupRecord, and add new InstanceRecord to it
   *
   * returns CF_ERR if group does not exist in this database
   *
   * Note that "group" is just a key.
   * addressof(found_group) != addressof(group)
   *
   */
  Result<void> AddInstance(const LocalInstanceGroup& group, const unsigned id);

  /*
   *  auto group = CF_EXPEC(FindGroups(...));
   *  RemoveInstanceGroup(group)
   */
  bool RemoveInstanceGroup(const LocalInstanceGroup& group);
  void Clear();

  Result<Set<LocalInstanceGroup>> FindGroups(const Query& query) const;
  Result<Set<LocalInstanceGroup>> FindGroups(const Queries& queries) const;
  Result<Set<LocalInstance>> FindInstances(const Query& query) const;
  Result<Set<LocalInstance>> FindInstances(const Queries& queries) const;
  const auto& InstanceGroups() const { return local_instance_groups_; }

  /*
   * FindGroup/Instance method must be used when exactly one instance/group
   * is expected to match the query
   */
  Result<LocalInstanceGroup> FindGroup(const Query& query) const;
  Result<LocalInstanceGroup> FindGroup(const Queries& queries) const;
  Result<LocalInstance> FindInstance(const Query& query) const;
  Result<LocalInstance> FindInstance(const Queries& queries) const;

 private:
  template <typename T>
  Result<Set<T>> Find(const Query& query,
                      const Map<FieldName, ConstHandler<T>>& handler_map) const;

  template <typename T>
  Result<Set<T>> Find(const Queries& queries,
                      const Map<FieldName, ConstHandler<T>>& handler_map) const;
  template <typename T>
  Result<T> FindOne(const Query& query,
                    const Map<FieldName, ConstHandler<T>>& handler_map) const;

  template <typename T>
  Result<T> FindOne(const Queries& queries,
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
