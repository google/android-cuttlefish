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

#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/instance_database_types.h"

namespace cuttlefish {
namespace selector {

class LocalInstanceGroup;

/**
 * TODO(kwstephenkim): add more methods, fields, and abstract out Instance
 *
 * Needs design changes to support both Remote and Local Instances
 */
class LocalInstance {
  friend class LocalInstanceGroup;
  friend class InstanceDatabase;

 public:
  static constexpr const char kJsonInstanceId[] = "Instance Id";
  static constexpr const char kJsonInstanceName[] = "Per-Instance Name";

  /* names:
   *
   * Many components in Cuttlefish traditionally expect the name to be "cvd-N,"
   * and rely on "N" to avoid conflicts in the global resource uses.
   *
   * Thus, we will eventually maintain the internal device name for those
   * existing cuttlefish implementation, and the user-given name.
   *
   */
  const std::string& InternalName() const;
  std::string InternalDeviceName() const;

  unsigned InstanceId() const;
  const std::string& PerInstanceName() const;
  std::string DeviceName() const;

  const LocalInstanceGroup& ParentGroup() const;

  class Copy {
    friend class LocalInstance;
    struct MockParentParam {
      std::string home_dir;
      std::string host_artifacts_path;
      std::string internal_group_name;
      std::string group_name;
      TimeStamp start_time;
    };

   public:
    /* when Copy is used, it is already disconnected from the original parent
     * group. Thus, it should carry the snapshot of needed information about
     * the parent group
     */
    class MockParent {
     public:
      MockParent(const MockParentParam&);
      const std::string& InternalGroupName() const {
        return internal_group_name_;
      }
      const std::string& GroupName() const { return group_name_; }
      const std::string& HomeDir() const { return home_dir_; }
      const std::string& HostArtifactsPath() const {
        return host_artifacts_path_;
      }
      auto StartTime() const { return start_time_; }

     private:
      std::string home_dir_;
      std::string host_artifacts_path_;
      std::string internal_group_name_;
      std::string group_name_;
      TimeStamp start_time_;
    };
    Copy(const LocalInstance& src);
    const std::string& InternalName() const { return internal_name_; }
    const std::string& InternalDeviceName() const {
      return internal_device_name_;
    }
    unsigned InstanceId() const { return instance_id_; }
    const std::string& PerInstanceName() const { return per_instance_name_; }
    const std::string& DeviceName() const { return device_name_; }
    const MockParent& ParentGroup() const { return mock_group_; }

   private:
    std::string internal_name_;
    std::string internal_device_name_;
    unsigned instance_id_;
    std::string per_instance_name_;
    std::string device_name_;
    MockParent mock_group_;
  };
  Copy GetCopy() const;

 private:
  LocalInstance(const LocalInstanceGroup& parent_group,
                const unsigned instance_id, const std::string& instance_name);

  const LocalInstanceGroup& parent_group_;
  unsigned instance_id_;
  std::string internal_name_;  ///< for now, it is to_string(instance_id_)
  /** the instance specific name to be appended to the group name
   *
   * by default, to_string(instance_id_). The default value is decided by
   * InstanceGroupRecord, as that's the only class that will create this
   * instance
   */
  std::string per_instance_name_;
};

}  // namespace selector
}  // namespace cuttlefish
