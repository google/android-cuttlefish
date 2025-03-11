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

#include <android-base/logging.h>

#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

static std::map<std::string, Json::ValueType> securitykeyMap = {
    {"serial_number", Json::ValueType::stringValue}};

static std::map<std::string, Json::ValueType> kernelkeyMap = {
    {"extra_kernel_cmdline", Json::ValueType::stringValue},
};

static std::map<std::string, Json::ValueType> kBootKeyMap = {
    {"extra_bootconfig_args", Json::ValueType::stringValue},
    {"security", Json::ValueType::objectValue},
    {"kernel", Json::ValueType::objectValue}};

Result<void> ValidateSecurityConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, securitykeyMap), "ValidateSecurityConfigs ValidateTypo fail");
  return {};
}

Result<void> ValidateKernelConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kernelkeyMap), "ValidateKernelConfigs ValidateTypo fail");
  return {};
}

Result<void> ValidateBootConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kBootKeyMap), "ValidateBootConfigs ValidateTypo fail");

  if (root.isMember("security")) {
    CF_EXPECT(ValidateSecurityConfigs(root["security"]), "ValidateSecurityConfigs fail");
  }

   if (root.isMember("kernel")) {
    CF_EXPECT(ValidateKernelConfigs(root["kernel"]), "ValidateKernelConfigs fail");
  }

  return {};
}

void InitBootConfigs(Json::Value& instances) {
  InitStringConfig(instances, "boot", "extra_bootconfig_args",
                   CF_DEFAULTS_EXTRA_BOOTCONFIG_ARGS);
  InitStringConfigSubGroup(instances, "boot", "security", "serial_number",
                           CF_DEFAULTS_SERIAL_NUMBER);
  InitStringConfigSubGroup(instances, "boot", "kernel", "extra_kernel_cmdline",
                           CF_DEFAULTS_EXTRA_KERNEL_CMDLINE);
}

void GenerateBootConfigs(const Json::Value& instances,
                         std::vector<std::string>& result) {

  result.emplace_back(GenerateStrGflag(instances, "extra_bootconfig_args", "boot",
                                    "extra_bootconfig_args"));
  result.emplace_back(GenerateStrGflagSubGroup(instances, "serial_number", "boot",
                                            "security", "serial_number"));
  result.emplace_back(GenerateStrGflagSubGroup(instances, "extra_kernel_cmdline",
                                            "boot", "kernel",
                                            "extra_kernel_cmdline"));
}

}  // namespace cuttlefish
