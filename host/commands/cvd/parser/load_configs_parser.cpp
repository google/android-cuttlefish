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

Result<Json::Value> ParseJsonFile(const std::string& file_path) {
  std::string file_content;
  using android::base::ReadFileToString;
  CF_EXPECT(ReadFileToString(file_path.c_str(), &file_content,
                             /* follow_symlinks */ true));
  auto root = CF_EXPECT(ParseJson(file_content), "Failed parsing JSON file");
  return root;
}

Result<void> ValidateCfConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kConfigsKeyMap), "Typo in config main parameters");
  CF_EXPECT(root.isMember("instances"), "instances object is missing");
  CF_EXPECT(ValidateInstancesConfigs(root["instances"]), "ValidateInstancesConfigs failed");

  return {};
}

std::string GenerateNumInstancesFlag(const Json::Value& root) {
  int num_instances = root["instances"].size();
  LOG(DEBUG) << "num_instances = " << num_instances;
  std::string result = "--num_instances=" + std::to_string(num_instances);
  return result;
}

std::vector<std::string> GenerateCfFlags(const Json::Value& root) {
  std::vector<std::string> result;
  result.emplace_back(GenerateNumInstancesFlag(root));

  result = MergeResults(result, GenerateInstancesFlags(root["instances"]));
  return result;
}

Result<std::vector<std::string>> ParseCvdConfigs(Json::Value& root) {
  CF_EXPECT(ValidateCfConfigs(root), "Loaded Json validation failed");

  InitInstancesConfigs(root["instances"]);

  return GenerateCfFlags(root);
  ;
}

}  // namespace cuttlefish
