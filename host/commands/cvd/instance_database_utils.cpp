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

#include "host/commands/cvd/instance_database_utils.h"

#include <string_view>

#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace instance_db {

std::string GenInternalGroupName() {
  std::string_view internal_name{kCvdNamePrefix};  // "cvd-"
  internal_name.remove_suffix(1);                  // "cvd"
  return std::string(internal_name);
}

std::string LocalDeviceNameRule(const std::string& group_name,
                                const std::string& instance_name) {
  return group_name + "-" + instance_name;
}

}  // namespace instance_db
}  // namespace cuttlefish
