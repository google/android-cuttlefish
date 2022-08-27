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

#include "host/commands/cvd/instance_group_record.h"

#include <android-base/file.h>
#include <android-base/logging.h>

#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace instance_db {

static std::string GenInternalGroupName() {
  std::string_view internal_name{kCvdNamePrefix};  // "cvd-"
  internal_name.remove_suffix(1);                  // "cvd"
  return std::string(internal_name);
}

LocalInstanceGroup::LocalInstanceGroup(const std::string& home_dir,
                                       const std::string& host_binaries_dir)
    : home_dir_{home_dir},
      host_binaries_dir_{host_binaries_dir},
      internal_group_name_(GenInternalGroupName()) {}

}  // namespace instance_db
}  // namespace cuttlefish
