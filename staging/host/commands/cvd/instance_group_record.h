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

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/libs/utils/result.h"
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
  using LocalInstanceGroupPtr = std::unique_ptr<LocalInstanceGroup>;

  const std::string& InternalGroupName() const { return internal_group_name_; }
  const std::string& HomeDir() const { return home_dir_; }
  const std::string& HostBinariesDir() const { return host_binaries_dir_; }
  const std::vector<LocalInstancePtr>& Instances() const { return instances_; }

 private:
  LocalInstanceGroup(const std::string& home_dir,
                     const std::string& host_binaries_dir);

  const std::string home_dir_;
  const std::string host_binaries_dir_;

  // for now, "cvd", which is "cvd-".remove_suffix(1)
  const std::string internal_group_name_;
  std::vector<LocalInstancePtr> instances_;
};

using LocalInstanceGroupPtr = LocalInstanceGroup::LocalInstanceGroupPtr;

}  // namespace instance_db
}  // namespace cuttlefish
