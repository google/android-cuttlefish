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

#include <optional>
#include <vector>

#include "internal_config.pb.h"
#include "user_config.pb.h"

#include "common/libs/utils/result.h"

namespace cuttlefish {

class AcloudConfig {
 public:
  AcloudConfig(const acloud::UserConfig&);
  ~AcloudConfig() = default;

 public:
  // UserConfig/user_config.proto members
  std::string launch_args;

  bool use_cvdr;

  // InternalConfig/internal_config.proto members

  // In both config
};

Result<const std::string> GetDefaultConfigFile(const uid_t uid);
Result<AcloudConfig> LoadAcloudConfig(const std::string& user_config_path,
                                      const uid_t uid);

}  // namespace cuttlefish
