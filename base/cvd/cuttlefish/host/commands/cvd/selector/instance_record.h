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
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_group_record.h"

namespace cuttlefish {
namespace selector {

struct InstanceGroupInfo;

/**
 * Needs design changes to support both Remote and Local Instances
 */
class LocalInstance {
 public:
  static constexpr const char kJsonInstanceId[] = "Instance Id";
  static constexpr const char kJsonInstanceName[] = "Per-Instance Name";

  LocalInstance(const cvd::InstanceGroup& parent_group,
                const cvd::Instance& instance_proto);

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

  unsigned InstanceId() const;
  const std::string& PerInstanceName() const;
  std::string DeviceName() const;
  std::string InternalDeviceName() const { return internal_device_name_; }
  const cvd::InstanceGroup& GroupProto() const { return group_proto_; }

 private:
  cvd::Instance instance_proto_;
  std::string internal_name_;  ///< for now, it is to_string(instance_id_)
  // Group specific information, repeated here because sometimes instances are
  // accessed outside of their group
  std::string internal_device_name_;
  cvd::InstanceGroup group_proto_;
};

}  // namespace selector
}  // namespace cuttlefish
