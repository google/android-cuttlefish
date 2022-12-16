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
    {"serial_number", Json::ValueType::stringValue},
    {"guest_enforce_security", Json::ValueType::booleanValue},
};

Result<void> ValidateSecurityConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kSecurityKeyMap),
            "ValidateSecurityConfigs ValidateTypo fail");
  return {};
}

/*This function is created to cover the initiation use_random_serial flag
when the json value of serial_number equal "@random"
*/
void InitRandomSerialNumber(Json::Value& instances) {
  int size = instances.size();
  for (int i = 0; i < size; i++) {
    std::string serial_number_str =
        instances[i]["security"]["serial_number"].asString();
    if (serial_number_str == "@random") {
      instances[i]["security"]["use_random_serial"] = true;
    } else {
      instances[i]["security"]["use_random_serial"] = false;
    }
  }
}

void InitSecurityConfigs(Json::Value& instances) {
  InitStringConfig(instances, "security", "serial_number",
                   CF_DEFAULTS_SERIAL_NUMBER);
  // This init should be called after the InitSecurityConfigs call, since it
  // depends on  serial_number flag
  InitRandomSerialNumber(instances);
  InitBoolConfig(instances, "security", "guest_enforce_security",
                 CF_DEFAULTS_GUEST_ENFORCE_SECURITY);
}

std::vector<std::string> GenerateSecurityFlags(const Json::Value& instances) {
  std::vector<std::string> result;
  if (!GENERATE_MVP_FLAGS_ONLY) {
    result.emplace_back(
        GenerateGflag(instances, "serial_number", "security", "serial_number"));
    result.emplace_back(GenerateGflag(instances, "use_random_serial",
                                      "security", "use_random_serial"));
  }
  result.emplace_back(GenerateGflag(instances, "guest_enforce_security",
                                    "security", "guest_enforce_security"));
  return result;
}

}  // namespace cuttlefish
