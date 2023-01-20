/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <android-base/file.h>
#include <gflags/gflags.h>

#include <stdio.h>
#include <fstream>
#include <string>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/cf_configs_instances.h"
#include "host/commands/cvd/parser/launch_cvd_parser.h"

namespace cuttlefish {

// json parameters definitions
static std::map<std::string, Json::ValueType> kConfigsKeyMap = {
    {"netsim_bt", Json::ValueType::booleanValue},
    {"instances", Json::ValueType::arrayValue}};

Result<void> ValidateCfConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kConfigsKeyMap),
            "Typo in config main parameters");
  CF_EXPECT(root.isMember("instances"), "instances object is missing");
  CF_EXPECT(ValidateInstancesConfigs(root["instances"]),
            "ValidateInstancesConfigs failed");

  return {};
}

std::string GenerateNumInstancesFlag(const Json::Value& root) {
  int num_instances = root["instances"].size();
  LOG(DEBUG) << "num_instances = " << num_instances;
  std::string result = "--num_instances=" + std::to_string(num_instances);
  return result;
}

std::string GenerateCommonGflag(const Json::Value& root,
                                const std::string& gflag_name,
                                const std::string& json_flag) {
  std::stringstream buff;
  // Append Header
  buff << "--" << gflag_name << "=" << root[json_flag].asString();
  return buff.str();
}

std::vector<std::string> GenerateCfFlags(const Json::Value& root) {
  std::vector<std::string> result;
  result.emplace_back(GenerateNumInstancesFlag(root));
  result.emplace_back(GenerateCommonGflag(root, "netsim_bt", "netsim_bt"));

  result = MergeResults(result, GenerateInstancesFlags(root["instances"]));
  return result;
}

void InitCvdConfigs(Json::Value& root) {
  // Handle common flags
  if (!root.isMember("netsim_bt")) {
    root["netsim_bt"] = CF_DEFAULTS_NETSIM_BT;
  }
  // Handle instances flags
  InitInstancesConfigs(root["instances"]);
}

Result<std::vector<std::string>> ParseLaunchCvdConfigs(Json::Value& root) {
  CF_EXPECT(ValidateCfConfigs(root), "Loaded Json validation failed");
  InitCvdConfigs(root);
  return GenerateCfFlags(root);
}

}  // namespace cuttlefish
