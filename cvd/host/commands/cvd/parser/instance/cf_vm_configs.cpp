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
#include <android-base/strings.h>

#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/instance/cf_vm_configs.h"
#include "host/libs/config/cuttlefish_config.h"

#define UI_DEFAULTS_MEMORY_MB 2048

namespace cuttlefish {

std::map<std::string, Json::ValueType> kCrosvmKeyMap = {
    {"enable_sandbox", Json::ValueType::booleanValue},
};

static std::map<std::string, Json::ValueType> kVmKeyMap = {
    {"cpus", Json::ValueType::intValue},
    {"memory_mb", Json::ValueType::intValue},
    {"use_sdcard", Json::ValueType::booleanValue},
    {"setupwizard_mode", Json::ValueType::stringValue},
    {"uuid", Json::ValueType::stringValue},
    {"crosvm", Json::ValueType::objectValue},
    {"qemu", Json::ValueType::objectValue},
    {"gem5", Json::ValueType::objectValue},
    {"custom_actions", Json::ValueType::arrayValue},
};

Result<void> ValidateVmConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kVmKeyMap),
            "ValidateVmConfigs ValidateTypo fail");
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
    if (instances[i].isMember("vm")) {
      if (instances[i]["vm"].isMember("crosvm")) {
        instances[i]["vm"]["vm_manager"] = "crosvm";
      } else if (instances[i]["vm"].isMember("qemu")) {
        instances[i]["vm"]["vm_manager"] = "qemu_cli";
      } else if (instances[i]["vm"].isMember("gem5")) {
        instances[i]["vm"]["vm_manager"] = "gem5";
      } else {
        // Set vm manager to default value (crosvm)
        instances[i]["vm"]["vm_manager"] = "crosvm";
      }
    } else {
      // vm object doesn't exist , set the default vm manager to crosvm
      instances[i]["vm"]["vm_manager"] = "crosvm";
    }
  }
}

void InitVmConfigs(Json::Value& instances) {
  InitIntConfig(instances, "vm", "cpus", CF_DEFAULTS_CPUS);
  InitIntConfig(instances, "vm", "memory_mb", UI_DEFAULTS_MEMORY_MB);
  InitBoolConfig(instances, "vm", "use_sdcard", CF_DEFAULTS_USE_SDCARD);
  InitStringConfig(instances, "vm", "setupwizard_mode",
                   CF_DEFAULTS_SETUPWIZARD_MODE);
  InitStringConfig(instances, "vm", "uuid", CF_DEFAULTS_UUID);
  InitVmManagerConfig(instances);
  InitBoolConfigSubGroup(instances, "vm", "crosvm", "enable_sandbox",
                         CF_DEFAULTS_ENABLE_SANDBOX);
}

std::vector<std::string> GenerateCustomConfigsFlags(
    const Json::Value& instances) {
  std::vector<std::string> result;
  int size = instances.size();
  for (int i = 0; i < size; i++) {
    if (instances[i].isMember("vm") &&
        instances[i]["vm"].isMember("custom_actions")) {
      Json::StreamWriterBuilder factory;
      std::string mapped_text =
          Json::writeString(factory, instances[i]["vm"]["custom_actions"]);
      // format json string string to match aosp/2374890 input format
      mapped_text = android::base::StringReplace(mapped_text, "\n", "", true);
      mapped_text = android::base::StringReplace(mapped_text, "\r", "", true);
      mapped_text =
          android::base::StringReplace(mapped_text, "\"", "\\\"", true);
      std::stringstream buff;
      buff << "--custom_actions=" << mapped_text;
      result.emplace_back(buff.str());
    } else {
      // custom_actions parameter doesn't exist in the configuration file
      result.emplace_back("--custom_actions=unset");
    }
  }
  return result;
}

std::vector<std::string> GenerateVmFlags(const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(GenerateGflag(instances, "cpus", "vm", "cpus"));
  result.emplace_back(GenerateGflag(instances, "memory_mb", "vm", "memory_mb"));
  result.emplace_back(
      GenerateGflag(instances, "use_sdcard", "vm", "use_sdcard"));
  result.emplace_back(
      GenerateGflag(instances, "vm_manager", "vm", "vm_manager"));
  result.emplace_back(
      GenerateGflag(instances, "setupwizard_mode", "vm", "setupwizard_mode"));
  if (!GENERATE_MVP_FLAGS_ONLY) {
    result.emplace_back(GenerateGflag(instances, "uuid", "vm", "uuid"));
  }
  result.emplace_back(GenerateGflagSubGroup(instances, "enable_sandbox", "vm",
                                            "crosvm", "enable_sandbox"));

  result = MergeResults(result, GenerateCustomConfigsFlags(instances));

  return result;
}

}  // namespace cuttlefish
