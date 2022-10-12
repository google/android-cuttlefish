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
#include <iostream>

#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/cf_configs_instances.h"
#include "host/commands/cvd/parser/instance/cf_vm_configs.h"

namespace cuttlefish {

static std::map<std::string, Json::ValueType> kInstanceKeyMap = {
    {"vm", Json::ValueType::objectValue},
    {"boot", Json::ValueType::objectValue},
    {"disk", Json::ValueType::objectValue},
    {"graphics", Json::ValueType::objectValue},
    {"camera", Json::ValueType::objectValue},
    {"connectivity", Json::ValueType::objectValue},
    {"audio", Json::ValueType::objectValue},
    {"streaming", Json::ValueType::objectValue},
    {"adb", Json::ValueType::objectValue},
    {"vehicle", Json::ValueType::objectValue},
    {"location", Json::ValueType::objectValue},
    {"metrics", Json::ValueType::objectValue}};

bool ValidateInstancesConfigs(const Json::Value& root) {
  int num_instances = root.size();
  for (unsigned int i = 0; i < num_instances; i++) {
    if (!ValidateTypo(root[i], kInstanceKeyMap)) {
      LOG(INFO) << "vm ValidateTypo fail";
      return false;
    }
    if (root[i].isMember("vm") && !ValidateVmConfigs(root[i]["vm"])) {
      LOG(INFO) << "ValidateVmConfigs fail";
      return false;
    }
  }

  return true;
}

void InitInstancesConfigs(Json::Value& root) { InitVmConfigs(root); }

void GenerateInstancesConfigs(const Json::Value& root,
                              std::vector<std::string>& result) {
  GenerateVmConfigs(root, result);
}

}  // namespace cuttlefish