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

#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/instance_group_record.h"

namespace cuttlefish {
namespace selector {

LocalInstance::LocalInstance(const LocalInstanceGroup& parent_group,
                             const unsigned instance_id,
                             const std::string& instance_name)
    : parent_group_(parent_group),
      instance_id_(instance_id),
      internal_name_(std::to_string(instance_id_)),
      per_instance_name_(instance_name) {}

unsigned LocalInstance::InstanceId() const { return instance_id_; }

std::string LocalInstance::InternalDeviceName() const {
  return LocalDeviceNameRule(parent_group_.InternalGroupName(), internal_name_);
}

const std::string& LocalInstance::InternalName() const {
  return internal_name_;
}

std::string LocalInstance::DeviceName() const {
  return LocalDeviceNameRule(parent_group_.GroupName(), per_instance_name_);
}

const std::string& LocalInstance::PerInstanceName() const {
  return per_instance_name_;
}

const LocalInstanceGroup& LocalInstance::ParentGroup() const {
  return parent_group_;
}

}  // namespace selector
}  // namespace cuttlefish
