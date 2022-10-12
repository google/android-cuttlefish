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
namespace cuttlefish {

static std::map<std::string, Json::ValueType> kVmKeyMap = {
    {"cpus", Json::ValueType::intValue}};

bool ValidateVmConfigs(const Json::Value& root) {
  if (!ValidateTypo(root, kVmKeyMap)) {
    LOG(INFO) << "ValidateVmConfigs ValidateTypo fail";
    return false;
  }
  return true;
}

void InitVmConfigs(Json::Value& root) {
  // Allocate and initialize with default values
  int size = root.size();
  for (int i = 0; i < size; i++) {
    if (!root[i].isMember("vm") || (!root[i]["vm"].isMember("cpus"))) {
      root[i]["vm"]["cpus"] = CF_DEFAULTS_CPUS;
    }
  }
}

std::string GenerateCpuFlag(const Json::Value& root) {
  int size = root.size();
  {
    std::stringstream buff;
    // Append Header
    buff << "--"
         << "cpus"
         << "=";
    // Append values
    for (int i = 0; i < size; i++) {
      int val = root[i]["vm"]["cpus"].asInt();
      buff << val;
      if (i != size - 1) buff << ",";
    }
    return buff.str();
  }
}

void GenerateVmConfigs(const Json::Value& root,
                       std::vector<std::string>& result) {
  result.emplace_back(GenerateCpuFlag(root));
}

}  // namespace cuttlefish