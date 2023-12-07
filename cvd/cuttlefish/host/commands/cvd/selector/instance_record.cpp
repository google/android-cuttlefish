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

#include "host/commands/cvd/instance_record.h"

#include "host/commands/cvd/instance_group_record.h"

namespace cuttlefish {
namespace instance_db {

LocalInstance::LocalInstance(const int instance_id, LocalInstanceGroup& parent)
    : instance_id_(instance_id),
      internal_name_(std::to_string(instance_id_)),
      parent_(parent) {}

int LocalInstance::InstanceId() const { return instance_id_; }

std::string LocalInstance::InternalDeviceName() const {
  return parent_.InternalGroupName() + "-" + internal_name_;
}

const std::string& LocalInstance::InternalName() const {
  return internal_name_;
}

const LocalInstanceGroup& LocalInstance::Parent() const { return parent_; }

}  // namespace instance_db
}  // namespace cuttlefish
