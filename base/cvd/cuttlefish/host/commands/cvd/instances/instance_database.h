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

#include <stddef.h>

#include <string>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/data_viewer.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class InstanceDatabase {
 public:
  // Filter is used to search for instances or groups based on their properties.
  // A group/instance matches the filter if it matches all of the specified
  // properties in the filter (effectively an AND operation, not an OR).
  struct Filter {
    std::optional<unsigned> instance_id;
    std::optional<std::string> group_name;
    // This property matches a group that contains instances with all these
    // names, even if it has other instances too. It matches an instance if the
    // instance name is the only element in the set (therefore if more than one
    // name is given it'll match no instances).
    std::unordered_set<std::string> instance_names;
    bool Empty() const;
  };

  InstanceDatabase(const std::string& backing_file);

  Result<bool> IsEmpty() const;

  /** Adds instance group.
   *
   * A new group name will be generated one is not provided.
   *
   * If group_name or home_dir is already taken or host_artifacts_path is
   * not likely an artifacts path, CF_ERR is returned.
   */
  Result<void> AddInstanceGroup(LocalInstanceGroup group);
  Result<void> UpdateInstanceGroup(const LocalInstanceGroup& group);

  Result<std::vector<LocalInstanceGroup>> InstanceGroups() const;
  Result<bool> RemoveInstanceGroup(const std::string& group_name);
  /**
   * Empties the database and returns the recently deleted instance groups.
   */
  Result<std::vector<LocalInstanceGroup>> Clear();

  Result<std::vector<LocalInstanceGroup>> FindGroups(
      const Filter& filter) const;

  /*
   * FindGroup/Instance method must be used when exactly one instance/group
   * is expected to match the filter
   */
  Result<LocalInstanceGroup> FindGroup(const Filter& filter) const {
    return ExactlyOne(FindGroups(filter));
  }
  Result<std::pair<LocalInstance, LocalInstanceGroup>> FindInstanceWithGroup(
      const Filter& filter) const;

 private:
  template <typename T>
  Result<T> ExactlyOne(Result<std::vector<T>>&& container_result) const {
    auto container = CF_EXPECT(std::move(container_result));
    CF_EXPECT_EQ(container.size(), static_cast<size_t>(1),
                 "Expected unique result");
    return *container.begin();
  }

  static std::vector<LocalInstanceGroup> FindGroups(
      const cvd::PersistentData& data, const Filter& filter);

  DataViewer viewer_;
};

}  // namespace cuttlefish
