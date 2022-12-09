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

std::map<std::string, Json::ValueType> kCrosvmKeyMap = {
    {"enable_sandbox", Json::ValueType::booleanValue},
};

static std::map<std::string, Json::ValueType> kVmKeyMap = {
    {"cpus", Json::ValueType::intValue},
    {"memory_mb", Json::ValueType::intValue},
    {"setupwizard_mode", Json::ValueType::stringValue},
    {"uuid", Json::ValueType::stringValue},
    {"crosvm", Json::ValueType::objectValue},
    {"qemu", Json::ValueType::objectValue},
    {"gem5", Json::ValueType::objectValue},
};

bool ValidateVmManager(const Json::Value& root) {
  bool result =
      root.isMember("crosvm") || root.isMember("qemu") || root.isMember("gem5");
  return result;
}

Result<void> ValidateVmConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kVmKeyMap), "ValidateVmConfigs ValidateTypo fail");
  CF_EXPECT(ValidateVmManager(root), "missing vm manager configs");
  if (root.isMember("crosvm")) {
    CF_EXPECT(ValidateTypo(root["crosvm"], kCrosvmKeyMap),
              "ValidateVmConfigs ValidateTypo crosvm fail");
  }
  return {};
}

void InitVmManagerConfig(Json::Value& instances) {
  // Allocate and initialize with default values
  int size = instances.size();
  for (int i = 0; i < size; i++) {
    if (instances[i].isMember("vm") && instances[i]["vm"].isMember("crosvm")) {
      instances[i]["vm"]["vm_manager"] = "crosvm";
    } else if (instances[i].isMember("vm") &&
               instances[i]["vm"].isMember("qemu")) {
      instances[i]["vm"]["vm_manager"] = "qemu_cli";
    } else if (instances[i].isMember("vm") &&
               instances[i]["vm"].isMember("gem5")) {
      instances[i]["vm"]["vm_manager"] = "gem5";
    } else {
      LOG(ERROR) << "Invalid VM manager configuration";
    }
  }
}

void InitVmConfigs(Json::Value& instances) {
  InitIntConfig(instances, "vm", "cpus", CF_DEFAULTS_CPUS);
  InitIntConfig(instances, "vm", "memory_mb", CF_DEFAULTS_MEMORY_MB);
  InitStringConfig(instances, "vm", "setupwizard_mode",
                   CF_DEFAULTS_SETUPWIZARD_MODE);
  InitStringConfig(instances, "vm", "uuid", CF_DEFAULTS_UUID);
  InitVmManagerConfig(instances);
  InitBoolConfigSubGroup(instances, "vm", "crosvm", "enable_sandbox",
                         CF_DEFAULTS_ENABLE_SANDBOX);
}

std::vector<std::string> GenerateVmFlags(const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(GenerateGflag(instances, "cpus", "vm", "cpus"));
  result.emplace_back(GenerateGflag(instances, "memory_mb", "vm", "memory_mb"));
  result.emplace_back(
      GenerateGflag(instances, "vm_manager", "vm", "vm_manager"));
  result.emplace_back(
      GenerateGflag(instances, "setupwizard_mode", "vm", "setupwizard_mode"));
  result.emplace_back(GenerateGflag(instances, "uuid", "vm", "uuid"));
  result.emplace_back(GenerateGflagSubGroup(instances, "enable_sandbox", "vm",
                                            "crosvm", "enable_sandbox"));

  return result;
}

}  // namespace cuttlefish
