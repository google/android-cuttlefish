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
#include "host/commands/cvd/parser/instance/cf_vm_configs.h"

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <json/json.h>

#include "common/libs/utils/result.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

#define UI_DEFAULTS_MEMORY_MB 2048

namespace cuttlefish {

void InitVmManagerConfig(Json::Value& instance) {
  if (instance.isMember("vm")) {
    if (instance["vm"].isMember("crosvm")) {
      instance["vm"]["vm_manager"] = "crosvm";
    } else if (instance["vm"].isMember("qemu")) {
      instance["vm"]["vm_manager"] = "qemu_cli";
    } else if (instance["vm"].isMember("gem5")) {
      instance["vm"]["vm_manager"] = "gem5";
    } else {
      // Set vm manager to default value (crosvm)
      instance["vm"]["vm_manager"] = "crosvm";
    }
  } else {
    // vm object doesn't exist, set the default vm manager to crosvm
    instance["vm"]["vm_manager"] = "crosvm";
  }
}

void InitVmConfigs(Json::Value& instances) {
  const int size = instances.size();
  for (int i = 0; i < size; i++) {
    InitConfig(instances[i], CF_DEFAULTS_CPUS, {"vm", "cpus"});
    InitConfig(instances[i], UI_DEFAULTS_MEMORY_MB, {"vm", "memory_mb"});
    InitConfig(instances[i], CF_DEFAULTS_USE_SDCARD, {"vm", "use_sdcard"});
    InitConfig(instances[i], CF_DEFAULTS_SETUPWIZARD_MODE,
               {"vm", "setupwizard_mode"});
    InitConfig(instances[i], CF_DEFAULTS_UUID, {"vm", "uuid"});
    InitVmManagerConfig(instances[i]);
    InitConfig(instances[i], CF_DEFAULTS_ENABLE_SANDBOX,
               {"vm", "crosvm", "enable_sandbox"});
  }
}

std::vector<std::string> GenerateCustomConfigsFlags(
    const Json::Value& instances) {
  std::vector<std::string> result;
  const int size = instances.size();
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

Result<std::vector<std::string>> GenerateVmFlags(const Json::Value& instances) {
  std::vector<std::string> result;
  result.emplace_back(
      CF_EXPECT(GenerateGflag(instances, "cpus", {"vm", "cpus"})));
  result.emplace_back(
      CF_EXPECT(GenerateGflag(instances, "memory_mb", {"vm", "memory_mb"})));
  result.emplace_back(
      CF_EXPECT(GenerateGflag(instances, "use_sdcard", {"vm", "use_sdcard"})));
  result.emplace_back(
      CF_EXPECT(GenerateGflag(instances, "vm_manager", {"vm", "vm_manager"})));
  result.emplace_back(CF_EXPECT(GenerateGflag(instances, "setupwizard_mode",
                                              {"vm", "setupwizard_mode"})));
  if (!GENERATE_MVP_FLAGS_ONLY) {
    result.emplace_back(
        CF_EXPECT(GenerateGflag(instances, "uuid", {"vm", "uuid"})));
  }
  result.emplace_back(CF_EXPECT(GenerateGflag(
      instances, "enable_sandbox", {"vm", "crosvm", "enable_sandbox"})));

  result = MergeResults(result, GenerateCustomConfigsFlags(instances));

  return result;
}

}  // namespace cuttlefish
