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

#include <android-base/file.h>
#include <gflags/gflags.h>

#include <stdio.h>
#include <fstream>
#include <string>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/cf_configs_instances.h"
#include "host/commands/cvd/parser/load_configs_parser.h"

namespace cuttlefish {

// json parameters definitions
static std::map<std::string, Json::ValueType> kConfigsKeyMap = {
    {"instances", Json::ValueType::arrayValue}};

Result<Json::Value> ParseJsonFile(std::string file_path) {
  std::string file_content;
  using android::base::ReadFileToString;
  CF_EXPECT(ReadFileToString(file_path.c_str(), &file_content,
                             /* follow_symlinks */ true));
  auto root = CF_EXPECT(ParseJson(file_content), "Failed parsing JSON file");
  return root;
}

bool ValidateCfConfigs(const Json::Value& root) {
  if (!ValidateTypo(root, kConfigsKeyMap)) {
    LOG(WARNING) << "Typo in config main parameters";
    return false;
  }
  if (!root.isMember("instances")) {
    LOG(WARNING) << "instances object is missing";
    return false;
  }

  if (!ValidateInstancesConfigs(root["instances"])) {
    return false;
  }

  return true;
}

void GenerateNumInstancesFlag(const Json::Value& root,
                              std::vector<std::string>& result) {
  int num_instances = root["instances"].size();
  LOG(DEBUG) << "num_instances = " << num_instances;
  std::string flag = "--num_instances=" + std::to_string(num_instances);
  result.push_back(flag);
}

void GenerateCfConfigs(const Json::Value& root,
                       std::vector<std::string>& result) {
  GenerateNumInstancesFlag(root, result);

  GenerateInstancesConfigs(root["instances"], result);
}

bool ParseCvdConfigs(Json::Value& root,
                     std::vector<std::string>& serialized_data) {
  if (!ValidateCfConfigs(root)) {
    LOG(WARNING) << "Loaded Json validation failed";
    return false;
  }

  InitInstancesConfigs(root["instances"]);

  GenerateCfConfigs(root, serialized_data);
  return true;
}

}  // namespace cuttlefish