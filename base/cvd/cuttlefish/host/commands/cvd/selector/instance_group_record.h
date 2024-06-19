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

#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"
#include "host/commands/cvd/selector/constant_reference.h"
#include "host/commands/cvd/selector/instance_database_types.h"

namespace cuttlefish {
namespace selector {

class LocalInstance;

class LocalInstanceGroup {
 public:
  static Result<LocalInstanceGroup> Create(const cvd::InstanceGroup& group_proto);

  LocalInstanceGroup(LocalInstanceGroup&&) = default;
  LocalInstanceGroup(const LocalInstanceGroup&) = default;
  LocalInstanceGroup& operator=(const LocalInstanceGroup&) = default;

  static Result<LocalInstanceGroup> Deserialize(const Json::Value& group_json);

  const std::string& InternalGroupName() const { return internal_group_name_; }
  const std::string& GroupName() const { return group_proto_.name(); }
  const std::string& HomeDir() const { return group_proto_.home_directory(); }
  void SetHomeDir(const std::string& home_dir);
  const std::string& HostArtifactsPath() const {
    return group_proto_.host_artifacts_path();
  }
  void SetHostArtifactsPath(const std::string& host_artifacts_path);
  const std::string& ProductOutPath() const {
    return group_proto_.product_out_path();
  }
  void SetProductOutPath(const std::string& product_out_path);
  TimeStamp StartTime() const;
  void SetStartTime(TimeStamp time);
  const std::vector<LocalInstance>& Instances() const { return instances_; }
  std::vector<LocalInstance>& Instances() { return instances_; }
  const cvd::InstanceGroup& Proto() const {
    return group_proto_;
  }
  void SetAllStates(cvd::InstanceState state);

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
  LocalInstanceGroup(const cvd::InstanceGroup& group_proto,
                     const std::vector<LocalInstance>& instances);

  std::string internal_group_name_;
  cvd::InstanceGroup group_proto_;
  std::vector<LocalInstance> instances_;
};

}  // namespace selector
}  // namespace cuttlefish
