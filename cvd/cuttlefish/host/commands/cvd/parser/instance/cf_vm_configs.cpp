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
#include <android-base/logging.h>

#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/instance/cf_vm_configs.h"
#include "host/libs/config/cuttlefish_config.h"
namespace cuttlefish {

static std::map<std::string, Json::ValueType> kVmKeyMap = {
    {"cpus", Json::ValueType::intValue},
    {"memory_mb", Json::ValueType::intValue},
    {"vm_manager", Json::ValueType::stringValue},
    {"setupwizard_mode", Json::ValueType::stringValue},
    {"uuid", Json::ValueType::stringValue},
};

Result<void> ValidateVmConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kVmKeyMap), "ValidateVmConfigs ValidateTypo fail");
  return {};
}

void InitVmConfigs(Json::Value& instances) {
  InitIntConfig(instances, "vm", "cpus", CF_DEFAULTS_CPUS);
  InitIntConfig(instances, "vm", "memory_mb", CF_DEFAULTS_MEMORY_MB);
  InitStringConfig(instances, "vm", "vm_manager", CF_DEFAULTS_VM_MANAGER);
  InitStringConfig(instances, "vm", "setupwizard_mode",
                   CF_DEFAULTS_SETUPWIZARD_MODE);
  InitStringConfig(instances, "vm", "uuid", CF_DEFAULTS_UUID);
}

std::vector<std::string> GenerateVmFlags(const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(GenerateIntGflag(instances, "cpus", "vm", "cpus"));
  result.emplace_back(GenerateIntGflag(instances, "memory_mb", "vm", "memory_mb"));
  result.emplace_back(
      GenerateStrGflag(instances, "vm_manager", "vm", "vm_manager"));
  result.emplace_back(
      GenerateStrGflag(instances, "setupwizard_mode", "vm", "setupwizard_mode"));
  result.emplace_back(GenerateStrGflag(instances, "uuid", "vm", "uuid"));
  return result;
}

}  // namespace cuttlefish
