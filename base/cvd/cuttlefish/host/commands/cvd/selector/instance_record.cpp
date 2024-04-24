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

#include "host/commands/cvd/selector/instance_record.h"

#include "cvd_persistent_data.pb.h"
#include "host/commands/cvd/selector/instance_database_utils.h"

namespace cuttlefish {
namespace selector {

LocalInstance::LocalInstance(const cvd::InstanceGroup& parent_group,
                             const cvd::Instance& instance_proto)
    : instance_proto_(instance_proto),
      internal_name_(std::to_string(instance_proto.id())),
      internal_device_name_(
          LocalDeviceNameRule(GenInternalGroupName(), internal_name_)),
      group_proto_(parent_group) {}

unsigned LocalInstance::InstanceId() const { return instance_proto_.id(); }

const std::string& LocalInstance::InternalName() const {
  return internal_name_;
}

std::string LocalInstance::DeviceName() const {
  return LocalDeviceNameRule(group_proto_.name(), instance_proto_.name());
}

const std::string& LocalInstance::PerInstanceName() const {
  return instance_proto_.name();
}

}  // namespace selector
}  // namespace cuttlefish
