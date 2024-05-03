/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/parser/cf_flags_validator.h"

#include "json/json.h"

#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"

namespace cuttlefish {

using cvd::config::Launch;

Result<Launch> ValidateCfConfigs(const Json::Value& root) {
  Launch launch_config;
  CF_EXPECT(Validate(root, launch_config), "Validation failure in [root object] ->");
  return launch_config;
}

}  // namespace cuttlefish
