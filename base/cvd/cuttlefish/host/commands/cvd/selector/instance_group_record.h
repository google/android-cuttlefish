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

#include <string>
#include <vector>

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/constant_reference.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_record.h"

namespace cuttlefish {
namespace selector {

class InstanceDatabase;

struct InstanceInfo {
  const unsigned id;
  const std::string name;
};

struct InstanceGroupInfo {
  std::string group_name;
  std::string home_dir;
  std::string host_artifacts_path;
  std::string product_out_path;
  TimeStamp start_time;
};

class LocalInstanceGroup {
 public:
  static Result<LocalInstanceGroup> Create(const InstanceGroupInfo&,
                                           const std::vector<InstanceInfo>&);

  LocalInstanceGroup(LocalInstanceGroup&&) = default;
  LocalInstanceGroup(const LocalInstanceGroup&) = default;
  LocalInstanceGroup& operator=(const LocalInstanceGroup&) = default;

  Json::Value Serialize() const;
  static Result<LocalInstanceGroup> Deserialize(const Json::Value& group_json);

  const std::string& InternalGroupName() const { return internal_group_name_; }
  const std::string& GroupName() const { return group_name_; }
  const std::string& HomeDir() const { return home_dir_; }
  const std::string& HostArtifactsPath() const { return host_artifacts_path_; }
  const std::string& ProductOutPath() const { return product_out_path_; }
  auto StartTime() const { return start_time_; }
  const std::vector<LocalInstance>& Instances() const { return instances_; }

  std::vector<LocalInstance> FindById(const unsigned id) const;
  /**
   * Find by per-instance name.
   *
   * If the device name is cvd-foo or cvd-4, "cvd" is the group name,
   * "foo" or "4" is the per-instance names, and "cvd-foo" or "cvd-4" is
   * the device name.
   */
  std::vector<LocalInstance> FindByInstanceName(
      const std::string& instance_name) const;

 private:
  LocalInstanceGroup(const std::string& group_name, const std::string& home_dir,
                     const std::string& host_artifacts_path,
                     const std::string& product_out_path,
                     const TimeStamp& start_time,
                     const std::vector<LocalInstance>& instances);

  Json::Value Serialize(const LocalInstance& instance) const;

  std::string internal_group_name_;
  std::string group_name_;
  std::string home_dir_;
  std::string host_artifacts_path_;
  std::string product_out_path_;
  TimeStamp start_time_;
  std::vector<LocalInstance> instances_;
};

}  // namespace selector
}  // namespace cuttlefish
