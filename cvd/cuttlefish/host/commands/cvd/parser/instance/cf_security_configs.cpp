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
#include "host/commands/cvd/parser/instance/cf_security_configs.h"

#include <android-base/logging.h>

#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

static std::map<std::string, Json::ValueType> kSecurityKeyMap = {
    {"serial_number", Json::ValueType::stringValue}};

Result<void> ValidateSecurityConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kSecurityKeyMap),
            "ValidateSecurityConfigs ValidateTypo fail");
  return {};
}

void InitSecurityConfigs(Json::Value& instances) {
  InitStringConfig(instances, "security", "serial_number",
                   CF_DEFAULTS_SERIAL_NUMBER);
}

std::vector<std::string> GenerateSecurityFlags(const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(GenerateStrGflag(instances, "serial_number", "security",
                                       "serial_number"));
  return result;
}

}  // namespace cuttlefish
