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
#include <vector>

#include <json/json.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database_types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_record.h"

namespace cuttlefish {

struct InstanceParams {
  const unsigned instance_id;
  const std::string per_instance_name;
  const cvd::InstanceState initial_state;
};

struct InstanceGroupParams {
  std::string group_name;
  std::vector<InstanceParams> instances;
};

class LocalInstanceGroup {
 public:
  static Result<LocalInstanceGroup> Create(InstanceGroupParams params);

  LocalInstanceGroup(LocalInstanceGroup&&) = default;
  LocalInstanceGroup(const LocalInstanceGroup&) = default;
  LocalInstanceGroup& operator=(const LocalInstanceGroup&) = default;

  const std::string& GroupName() const { return group_proto_->name(); }
  const std::string& HomeDir() const { return group_proto_->home_directory(); }
  const std::string& HostArtifactsPath() const {
    return group_proto_->host_artifacts_path();
  }
  const std::string& ProductOutPath() const {
    return group_proto_->product_out_path();
  }
  TimeStamp StartTime() const;
  void SetStartTime(TimeStamp time);
  const std::vector<LocalInstance>& Instances() const { return instances_; }
  std::vector<LocalInstance>& Instances() { return instances_; }
  bool HasActiveInstances() const;
  const cvd::InstanceGroup& Proto() const { return *group_proto_; }
  void SetAllStates(cvd::InstanceState state);

  std::string BaseDir() const;
  std::string AssemblyDir() const;
  std::string MetricsDir() const;
  std::string ArtifactsDir() const;
  std::string ProductDir(int instance_index) const;

  Result<LocalInstance> FindInstanceById(unsigned id) const;
  /**
   * Find by per-instance name.
   *
   * If the device name is cvd-foo or cvd-4, "cvd" is the group name,
   * "foo" or "4" is the per-instance names, and "cvd-foo" or "cvd-4" is
   * the device name.
   */
  std::vector<LocalInstance> FindByInstanceName(
      const std::string& instance_name) const;
  // Fetches status from all instances in the group. Waits for run_cvd to
  // respond for at most timeout seconds for each instance.
  Result<Json::Value> FetchStatus(
      std::chrono::seconds timeout = std::chrono::seconds(5));

 private:
  friend class InstanceDatabase;

  static Result<LocalInstanceGroup> Create(
      const cvd::InstanceGroup& group_proto);

  LocalInstanceGroup(const cvd::InstanceGroup& group_proto);

  // Ownership of the proto is shared between the LocalInstanceGroup and
  // LocalInstance classes to ensure the references the latter maintains remain
  // valid if the LocalInstanceGroup is destroyed before it.
  std::shared_ptr<cvd::InstanceGroup> group_proto_;
  std::vector<LocalInstance> instances_;
};

}  // namespace cuttlefish
