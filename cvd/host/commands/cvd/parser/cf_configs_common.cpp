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
#include "host/commands/cvd/parser/cf_configs_common.h"

#include <android-base/logging.h>
namespace cuttlefish {

/**
 * Validate Json data Name and type
 */
bool ValidateTypo(const Json::Value& root,
                  const std::map<std::string, Json::ValueType>& map) {
  for (const std::string& flag : root.getMemberNames()) {
    if (map.count(flag) == 0) {
      LOG(WARNING) << "Invalid flag name (typo) , Param --> " << flag
                   << " not recognized";
      return false;
    }
    if (!root[flag].isConvertibleTo(map.at(flag))) {
      LOG(WARNING) << "Invalid flag type " << flag;
      return false;
    }
  }
  return true;
}

void InitIntConfig(Json::Value& instances, std::string group,
                   std::string json_flag, int default_value) {
  // Allocate and initialize with default values
  int size = instances.size();
  for (int i = 0; i < size; i++) {
    if (!instances[i].isMember(group) ||
        (!instances[i][group].isMember(json_flag))) {
      instances[i][group][json_flag] = default_value;
    }
  }
}

void InitStringConfig(Json::Value& instances, std::string group,
                      std::string json_flag, std::string default_value) {
  // Allocate and initialize with default values
  int size = instances.size();
  for (int i = 0; i < size; i++) {
    if (!instances[i].isMember(group) ||
        (!instances[i][group].isMember(json_flag))) {
      instances[i][group][json_flag] = default_value;
    }
  }
}

void InitStringConfigSubGroup(Json::Value& instances, std::string group,
                              std::string subgroup, std::string json_flag,
                              std::string default_value) {
  // Allocate and initialize with default values
  int size = instances.size();
  for (int i = 0; i < size; i++) {
    if (!instances[i].isMember(group) ||
        (!instances[i][group].isMember(subgroup)) ||
        (!instances[i][group][subgroup].isMember(json_flag))) {
      instances[i][group][subgroup][json_flag] = default_value;
    }
  }
}

std::string GenerateGflag(const Json::Value& instances, std::string gflag_name,
                          std::string group, std::string json_flag) {
  int size = instances.size();
  std::stringstream buff;
  // Append Header
  buff << "--" << gflag_name << "=";
  // Append values
  for (int i = 0; i < size; i++) {
    buff << instances[i][group][json_flag];
    if (i != size - 1) buff << ",";
  }
  return buff.str();
}

std::string GenerateGflagSubGroup(const Json::Value& instances,
                                  std::string gflag_name, std::string group,
                                  std::string subgroup, std::string json_flag) {
  int size = instances.size();
  std::stringstream buff;
  // Append Header
  buff << "--" << gflag_name << "=";
  // Append values
  for (int i = 0; i < size; i++) {
    buff << instances[i][group][subgroup][json_flag];
    if (i != size - 1) buff << ",";
  }
  return buff.str();
}
}  // namespace cuttlefish