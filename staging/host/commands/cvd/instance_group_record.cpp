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

Result<void> LocalInstanceGroup::AddInstance(const int instance_id) {
  if (HasInstance(instance_id)) {
    return CF_ERR("Instance Id " << instance_id << " is taken");
  }
  auto new_instance = LocalInstance::Create(instance_id, *this);
  instances_.emplace_back(std::move(new_instance));
  return {};
}

Result<void> LocalInstanceGroup::AddInstance(LocalInstancePtr instance) {
  if (!instance) {
    CF_ERR("instance to add is nullptr");
  }
  const auto instance_id = instance->InstanceId();
  if (HasInstance(instance_id)) {
    return CF_ERR("Instance Id " << instance_id << " is taken");
  }
  instances_.emplace_back(std::move(instance));
  return {};
}

bool LocalInstanceGroup::HasInstance(const int instance_id) const {
  for (const auto& instance : instances_) {
    if (instance_id == instance->InstanceId()) {
      return true;
    }
  }
  return false;
}

Result<std::string> LocalInstanceGroup::GetCuttlefishConfigPath() const {
  std::string home_realpath;
  if (DirectoryExists(HomeDir())) {
    CF_EXPECT(android::base::Realpath(HomeDir(), &home_realpath));
    static const char kSuffix[] = "/cuttlefish_assembly/cuttlefish_config.json";
    std::string config_path = AbsolutePath(home_realpath + kSuffix);
    if (FileExists(config_path)) {
      return config_path;
    }
  }
  return {};
}

}  // namespace instance_db
}  // namespace cuttlefish
