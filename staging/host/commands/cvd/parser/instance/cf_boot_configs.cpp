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
#include "host/commands/cvd/parser/instance/cf_boot_configs.h"

#include <string>
#include <vector>

#include <json/json.h>

#include "common/libs/utils/result.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {

Result<void> InitBootConfigs(Json::Value& instances) {
  for (auto& instance : instances) {
    CF_EXPECT(InitConfig(instance, CF_DEFAULTS_ENABLE_BOOTANIMATION,
                         {"boot", "enable_bootanimation"}));
  }
  return {};
}

Result<std::vector<std::string>> GenerateBootFlags(
    const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(CF_EXPECT(GenerateGflag(
      instances, "enable_bootanimation", {"boot", "enable_bootanimation"})));
  return result;
}

}  // namespace cuttlefish
