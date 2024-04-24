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
#include "host/commands/cvd/selector/data_viewer.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"

namespace cuttlefish {
namespace selector {

class InstanceDatabase {
 public:
  InstanceDatabase(const std::string& backing_file);

  Result<bool> IsEmpty() const;

  Result<Json::Value> Serialize() const;
  Result<void> LoadFromJson(const Json::Value&);

  /** Adds instance group.
   *
   * If group_name or home_dir is already taken or host_artifacts_path is
   * not likely an artifacts path, CF_ERR is returned.
   */
  Result<void> AddInstanceGroup(
      const InstanceGroupInfo& group_info,
      const std::vector<InstanceInfo>& instance_infos);

  Result<std::vector<LocalInstanceGroup>> InstanceGroups() const;
  Result<bool> RemoveInstanceGroup(const std::string& group_name);
  /**
   * Empties the database and returns the recently deleted instance groups.
   */
  Result<std::vector<LocalInstanceGroup>> Clear();

  Result<std::vector<LocalInstanceGroup>> FindGroups(const Query& query) const {
    return FindGroups(Queries{query});
  }
  Result<std::vector<LocalInstanceGroup>> FindGroups(
      const Queries& queries) const {
    return FindGroups(CF_EXPECT(ParamFromQueries(queries)));
  }
  Result<std::vector<LocalInstance>> FindInstances(const Query& query) const {
    return FindInstances(Queries{query});
  }
  Result<std::vector<LocalInstance>> FindInstances(
      const Queries& queries) const {
    return FindInstances(CF_EXPECT(ParamFromQueries(queries)));
  }

  /*
   * FindGroup/Instance method must be used when exactly one instance/group
   * is expected to match the query
   */
  Result<LocalInstanceGroup> FindGroup(const Query& query) const {
    return ExactlyOne(FindGroups(query));
  }
  Result<LocalInstanceGroup> FindGroup(const Queries& queries) const {
    return ExactlyOne(FindGroups(queries));
  }
  Result<LocalInstance> FindInstance(const Query& query) const {
    return ExactlyOne(FindInstances(query));
  }
  Result<LocalInstance> FindInstance(const Queries& queries) const {
    return ExactlyOne(FindInstances(queries));
  }

 private:
  template <typename T>
  Result<T> ExactlyOne(Result<std::vector<T>>&& container_result) const {
    auto container = CF_EXPECT(std::move(container_result));
    CF_EXPECT_EQ(container.size(), 1, "Expected unique result");
    return {*container.begin()};
  }
  struct FindParam {
    std::optional<Value> home;
    std::optional<unsigned> id;
    std::optional<Value> group_name;
    std::optional<Value> instance_name;
  };
  Result<FindParam> ParamFromQueries(const Queries&) const;
  Result<std::vector<LocalInstanceGroup>> FindGroups(FindParam param) const;
  Result<std::vector<LocalInstance>> FindInstances(FindParam param) const;
  static std::vector<LocalInstanceGroup> FindGroups(
      const std::vector<LocalInstanceGroup>& groups, FindParam param);
  static std::vector<LocalInstance> FindInstances(
      const std::vector<LocalInstanceGroup>& groups, FindParam param);

  DataViewer<std::vector<LocalInstanceGroup>> viewer_;
};

}  // namespace selector
}  // namespace cuttlefish
