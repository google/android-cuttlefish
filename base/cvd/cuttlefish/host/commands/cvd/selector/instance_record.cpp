/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <android-base/logging.h>
#include <fmt/format.h>

namespace cuttlefish {
namespace selector {

namespace {
constexpr int BASE_ADB_PORT = 6520;
constexpr int BASE_INSTANCE_ID = 1;
}  // namespace

LocalInstance::LocalInstance(std::shared_ptr<cvd::InstanceGroup> group_proto,
                             cvd::Instance* instance_proto)
    : group_proto_(group_proto), instance_proto_(instance_proto) {}

void LocalInstance::set_state(cvd::InstanceState state) {
  instance_proto_->set_state(state);
}

std::string LocalInstance::instance_dir() const {
  return fmt::format("{}/cuttlefish/instances/cvd-{}",
                     group_proto_->home_directory(), id());
}

int LocalInstance::adb_port() const {
  // The instance id is zero for a very short time between the load and create
  // commands. The adb_port property should not be accessed during that time,
  // but return an invalid port number just in case.
  if (id() == 0) {
    return 0;
  }
  // run_cvd picks this port from the instance id and doesn't provide a flag
  // to change in cvd_internal_flag
  return BASE_ADB_PORT + id() - BASE_INSTANCE_ID;
}

bool LocalInstance::IsActive() const {
  switch (state()) {
    case cvd::INSTANCE_STATE_RUNNING:
    case cvd::INSTANCE_STATE_STARTING:
    case cvd::INSTANCE_STATE_STOPPING:
    case cvd::INSTANCE_STATE_PREPARING:
    case cvd::INSTANCE_STATE_UNREACHABLE:
      return true;
    case cvd::INSTANCE_STATE_UNSPECIFIED:
    case cvd::INSTANCE_STATE_STOPPED:
    case cvd::INSTANCE_STATE_PREPARE_FAILED:
    case cvd::INSTANCE_STATE_BOOT_FAILED:
    case cvd::INSTANCE_STATE_CANCELLED:
      return false;
    // Include these just to avoid the warning
    default:
      LOG(FATAL) << "Invalid instance state: " << state();
  }
  return false;
}

}  // namespace selector
}  // namespace cuttlefish
