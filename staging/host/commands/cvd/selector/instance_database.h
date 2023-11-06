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
#include <memory>
#include <vector>

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/constant_reference.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"

namespace cuttlefish {
namespace selector {

// TODO(kwstephenkim): make this per-user instance database
class InstanceDatabase {
  template <typename T>
  using ConstHandler = std::function<Result<Set<ConstRef<T>>>(const Value&)>;

  using ConstGroupHandler = ConstHandler<LocalInstanceGroup>;
  using ConstInstanceHandler = ConstHandler<LocalInstance>;

 public:
  InstanceDatabase();
  bool IsEmpty() const;

  struct AddInstanceGroupParam {
    std::string group_name;
    std::string home_dir;
    std::string host_artifacts_path;
    std::string product_out_path;
  };
  /** Adds instance group.
   *
   * If group_name or home_dir is already taken or host_artifacts_path is
   * not likely an artifacts path, CF_ERR is returned.
   */
  Result<ConstRef<LocalInstanceGroup>> AddInstanceGroup(
      const AddInstanceGroupParam& param);

  Json::Value Serialize() const;
  Result<void> LoadFromJson(const Json::Value&);

  /**
   * Adds instance to the group.
   *
   * If id is duplicated in the scope of the InstanceDatabase or instance_name
   * is not unique within the group, CF_ERR is returned.
   */
  Result<void> AddInstance(const std::string& group_name, const unsigned id,
                           const std::string& instance_name);

  struct InstanceInfo {
    const unsigned id;
    const std::string name;
  };
  Result<void> AddInstances(const std::string& group_name,
                            const std::vector<InstanceInfo>& instances);

  Result<void> SetBuildId(const std::string& group_name,
                          const std::string& build_id);

  /*
   *  auto group = CF_EXPEC(FindGroups(...));
   *  RemoveInstanceGroup(group)
   */
  bool RemoveInstanceGroup(const LocalInstanceGroup& group);
  bool RemoveInstanceGroup(const std::string& group_name);
  void Clear();

  Result<Set<ConstRef<LocalInstanceGroup>>> FindGroups(
      const Query& query) const;
  Result<Set<ConstRef<LocalInstanceGroup>>> FindGroups(
      const Queries& queries) const;
  Result<Set<ConstRef<LocalInstance>>> FindInstances(const Query& query) const;
  Result<Set<ConstRef<LocalInstance>>> FindInstances(
      const Queries& queries) const;
  const auto& InstanceGroups() const { return local_instance_groups_; }

  /*
   * FindGroup/Instance method must be used when exactly one instance/group
   * is expected to match the query
   */
  Result<ConstRef<LocalInstanceGroup>> FindGroup(const Query& query) const;
  Result<ConstRef<LocalInstanceGroup>> FindGroup(const Queries& queries) const;
  Result<ConstRef<LocalInstance>> FindInstance(const Query& query) const;
  Result<ConstRef<LocalInstance>> FindInstance(const Queries& queries) const;

 private:
  template <typename T>
  Result<Set<ConstRef<T>>> Find(
      const Query& query,
      const Map<FieldName, ConstHandler<T>>& handler_map) const;

  template <typename T>
  Result<Set<ConstRef<T>>> Find(
      const Queries& queries,
      const Map<FieldName, ConstHandler<T>>& handler_map) const;

  template <typename T>
  Result<ConstRef<T>> FindOne(
      const Query& query,
      const Map<FieldName, ConstHandler<T>>& handler_map) const;

  template <typename T>
  Result<ConstRef<T>> FindOne(
      const Queries& queries,
      const Map<FieldName, ConstHandler<T>>& handler_map) const;

  std::vector<std::unique_ptr<LocalInstanceGroup>>::iterator FindIterator(
      const LocalInstanceGroup& group);

  // actual Find implementations
  Result<Set<ConstRef<LocalInstanceGroup>>> FindGroupsByHome(
      const Value& home) const;
  Result<Set<ConstRef<LocalInstanceGroup>>> FindGroupsById(
      const Value& id) const;
  Result<Set<ConstRef<LocalInstanceGroup>>> FindGroupsByGroupName(
      const Value& group_name) const;
  Result<Set<ConstRef<LocalInstanceGroup>>> FindGroupsByInstanceName(
      const Value& instance_name) const;
  Result<Set<ConstRef<LocalInstance>>> FindInstancesByHome(
      const Value& home) const;
  Result<Set<ConstRef<LocalInstance>>> FindInstancesById(const Value& id) const;
  Result<Set<ConstRef<LocalInstance>>> FindInstancesByGroupName(
      const Value& instance_specific_name) const;
  Result<Set<ConstRef<LocalInstance>>> FindInstancesByInstanceName(
      const Value& group_name) const;

  Result<LocalInstanceGroup*> FindMutableGroup(const std::string& group_name);

  Result<void> LoadGroupFromJson(const Json::Value& group_json);

  std::vector<std::unique_ptr<LocalInstanceGroup>> local_instance_groups_;
  Map<FieldName, ConstGroupHandler> group_handlers_;
  Map<FieldName, ConstInstanceHandler> instance_handlers_;

  static constexpr const char kJsonGroups[] = "Groups";
};

}  // namespace selector
}  // namespace cuttlefish
