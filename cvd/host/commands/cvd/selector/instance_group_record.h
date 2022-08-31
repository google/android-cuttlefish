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
#include <string>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/instance_database_types.h"
#include "host/commands/cvd/instance_record.h"

namespace cuttlefish {
namespace instance_db {

/**
 * TODO(kwstephenkim): add more methods, fields, and abstract out Instance
 *
 * Needs design changes to support both Remote Instances
 */
class LocalInstanceGroup {
 public:
  LocalInstanceGroup(const std::string& home_dir,
                     const std::string& host_binaries_dir);
  LocalInstanceGroup(LocalInstanceGroup&&) = default;
  LocalInstanceGroup(const LocalInstanceGroup&) = default;
  LocalInstanceGroup& operator=(LocalInstanceGroup&&) = default;
  LocalInstanceGroup& operator=(const LocalInstanceGroup&) = default;
  // TODO(stephenkim): Replace with default operator== in C++20
  bool operator==(const LocalInstanceGroup& target) const {
    return Compare(target);
  }

  const std::string& InternalGroupName() const { return internal_group_name_; }
  const std::string& HomeDir() const { return home_dir_; }
  const std::string& HostBinariesDir() const { return host_binaries_dir_; }
  Result<std::string> GetCuttlefishConfigPath() const;
  const Set<LocalInstance> Instances() const { return instances_; }
  /**
   * return error if instance id of instance is taken AND that taken id
   * belongs to this group
   */
  Result<void> AddInstance(const int instance_id);
  Result<void> AddInstance(const LocalInstance& instance);
  bool HasInstance(const int instance_id) const;
  std::size_t HashCode() const noexcept;

 private:
  bool Compare(const LocalInstanceGroup& target) const {
    // list all fields
    return (home_dir_ == target.home_dir_) &&
           (host_binaries_dir_ == target.host_binaries_dir_) &&
           (internal_group_name_ == target.internal_group_name_) &&
           (instances_ == target.instances_);
  }
  std::string home_dir_;
  std::string host_binaries_dir_;

  // for now, "cvd", which is "cvd-".remove_suffix(1)
  std::string internal_group_name_;
  Set<LocalInstance> instances_;
};

}  // namespace instance_db
}  // namespace cuttlefish

template <>
struct std::hash<cuttlefish::instance_db::LocalInstanceGroup> {
  using LocalInstanceGroup = cuttlefish::instance_db::LocalInstanceGroup;
  std::size_t operator()(const LocalInstanceGroup& group) const noexcept {
    return group.HashCode();
  }
};
