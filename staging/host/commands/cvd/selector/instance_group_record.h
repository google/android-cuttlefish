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

#include <gtest/gtest.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/constant_reference.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_record.h"

namespace cuttlefish {
namespace selector {

class InstanceDatabase;

/**
 * TODO(kwstephenkim): add more methods, fields, and abstract out Instance
 *
 * Needs design changes to support both Remote Instances
 */
class LocalInstanceGroup {
  friend InstanceDatabase;

 public:
  const std::string& InternalGroupName() const { return internal_group_name_; }
  const std::string& GroupName() const { return group_name_; }
  const std::string& HomeDir() const { return home_dir_; }
  const std::string& HostArtifactsPath() const { return host_artifacts_path_; }
  Result<std::string> GetCuttlefishConfigPath() const;
  const Set<std::unique_ptr<LocalInstance>>& Instances() const {
    return instances_;
  }
  /**
   * return error if instance id of instance is taken AND that taken id
   * belongs to this group
   */
  Result<void> AddInstance(const unsigned instance_id,
                           const std::string& instance_name);
  bool HasInstance(const unsigned instance_id) const;

  Result<Set<ConstRef<LocalInstance>>> FindById(const unsigned id) const;
  /**
   * Find by per-instance name.
   *
   * If the device name is cvd-foo or cvd-4, "cvd" is the group name,
   * "foo" or "4" is the per-instance names, and "cvd-foo" or "cvd-4" is
   * the device name.
   */
  Result<Set<ConstRef<LocalInstance>>> FindByInstanceName(
      const std::string& instance_name) const;

 private:
  LocalInstanceGroup(const std::string& group_name, const std::string& home_dir,
                     const std::string& host_artifacts_path);
  std::string home_dir_;
  std::string host_artifacts_path_;

  // for now, "cvd", which is "cvd-".remove_suffix(1)
  std::string internal_group_name_;
  std::string group_name_;
  Set<std::unique_ptr<LocalInstance>> instances_;

  /*
   * Expose constructor to the tests in InstanceRecord unit test suite.
   *
   * To create InstanceRecords, we should create InstanceGroup first.
   */
  FRIEND_TEST(CvdInstanceRecordUnitTest, Fields);

  friend class CvdInstanceGroupUnitTest;
  friend class CvdInstanceGroupSearchUnitTest;
};

}  // namespace selector
}  // namespace cuttlefish
