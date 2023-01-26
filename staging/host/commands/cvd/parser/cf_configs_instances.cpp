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

#include "host/commands/cvd/parser/cf_configs_instances.h"

#include <android-base/logging.h>
#include <iostream>
#include <unordered_set>

#include "common/libs/utils/flags_validator.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/instance/cf_boot_configs.h"
#include "host/commands/cvd/parser/instance/cf_graphics_configs.h"
#include "host/commands/cvd/parser/instance/cf_metrics_configs.h"
#include "host/commands/cvd/parser/instance/cf_security_configs.h"
#include "host/commands/cvd/parser/instance/cf_vm_configs.h"

namespace cuttlefish {

static std::map<std::string, Json::ValueType> kInstanceKeyMap = {
    {"@import", Json::ValueType::stringValue},
    {"vm", Json::ValueType::objectValue},
    {"boot", Json::ValueType::objectValue},
    {"security", Json::ValueType::objectValue},
    {"disk", Json::ValueType::objectValue},
    {"graphics", Json::ValueType::objectValue},
    {"camera", Json::ValueType::objectValue},
    {"connectivity", Json::ValueType::objectValue},
    {"audio", Json::ValueType::objectValue},
    {"streaming", Json::ValueType::objectValue},
    {"adb", Json::ValueType::objectValue},
    {"vehicle", Json::ValueType::objectValue},
    {"location", Json::ValueType::objectValue}};

static std::unordered_set<std::string> kSupportedImportValues = {
    "phone", "tablet", "tv", "wearable", "auto", "slim", "go", "foldable"};

Result<void> ValidateInstancesConfigs(const Json::Value& root) {
  int num_instances = root.size();
  for (unsigned int i = 0; i < num_instances; i++) {
    CF_EXPECT(ValidateTypo(root[i], kInstanceKeyMap), "vm ValidateTypo fail");

    if (root[i].isMember("vm")) {
      CF_EXPECT(ValidateVmConfigs(root[i]["vm"]), "ValidateVmConfigs fail");
    }

    // Validate @import flag values are supported or not
    if (root[i].isMember("@import")) {
      CF_EXPECT(kSupportedImportValues.count(root[i]["@import"].asString()) > 0,
                "@Import flag values are not supported");
    }

    if (root[i].isMember("boot")) {
      CF_EXPECT(ValidateBootConfigs(root[i]["boot"]), "ValidateBootConfigs fail");
    }
    if (root[i].isMember("security")) {
      CF_EXPECT(ValidateSecurityConfigs(root[i]["security"]),
                "ValidateSecurityConfigs fail");
    }
    if (root[i].isMember("graphics")) {
      CF_EXPECT(ValidateGraphicsConfigs(root[i]["graphics"]),
                "ValidateGraphicsConfigs fail");
    }
  }
  CF_EXPECT(ValidateStringConfig(root, "vm", "setupwizard_mode",
                                 ValidateStupWizardMode),
            "Invalid value for setupwizard_mode flag");

  return {};
}

void InitInstancesConfigs(Json::Value& root) {
  InitVmConfigs(root);
  InitBootConfigs(root);
  InitSecurityConfigs(root);
  InitGraphicsConfigs(root);
}

std::vector<std::string> GenerateInstancesFlags(const Json::Value& root) {
  std::vector<std::string> result = GenerateVmFlags(root);
  if (!GENERATE_MVP_FLAGS_ONLY) {
    result = MergeResults(result, GenerateBootFlags(root));
  }
  result = MergeResults(result, GenerateSecurityFlags(root));
  result = MergeResults(result, GenerateGraphicsFlags(root));
  result = MergeResults(result, GenerateMetricsFlags(root));

  return result;
}

}  // namespace cuttlefish
