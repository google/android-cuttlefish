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
Result<void> ValidateTypo(const Json::Value& root,
                          const std::map<std::string, Json::ValueType>& map) {
  for (const std::string& flag : root.getMemberNames()) {
    CF_EXPECT(map.count(flag) != 0 , "Invalid flag name (typo) , Param --> " << flag<< " not recognized");
    CF_EXPECT(root[flag].isConvertibleTo(map.at(flag)), "Invalid flag typ"<< flag);
  }
  return {};
}

void InitIntConfig(Json::Value& instances, const std::string& group,
                   const std::string& json_flag, int default_value) {
  // Allocate and initialize with default values
  int size = instances.size();
  for (int i = 0; i < size; i++) {
    if (!instances[i].isMember(group) ||
        (!instances[i][group].isMember(json_flag))) {
      instances[i][group][json_flag] = default_value;
    }
  }
}

void InitStringConfig(Json::Value& instances, const std::string& group,
                      const std::string& json_flag, const std::string& default_value) {
  // Allocate and initialize with default values
  int size = instances.size();
  for (int i = 0; i < size; i++) {
    if (!instances[i].isMember(group) ||
        (!instances[i][group].isMember(json_flag))) {
      instances[i][group][json_flag] = default_value;
    }
  }
}
void InitBoolConfig(Json::Value& instances, const std::string& group,
                    const std::string& json_flag, const bool default_value) {
  // Allocate and initialize with default values
  int size = instances.size();
  for (int i = 0; i < size; i++) {
    if (!instances[i].isMember(group) ||
        (!instances[i][group].isMember(json_flag))) {
      instances[i][group][json_flag] = default_value;
    }
  }
}

void InitStringConfigSubGroup(Json::Value& instances, const std::string& group,
                              const std::string& subgroup, const std::string& json_flag,
                              const std::string& default_value) {
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

std::string GenerateIntGflag(const Json::Value& instances, const std::string& gflag_name,
                          const std::string& group, const std::string& json_flag) {
  int size = instances.size();
  std::stringstream buff;
  // Append Header
  buff << "--" << gflag_name << "=";
  // Append values
  for (int i = 0; i < size; i++) {
    buff << instances[i][group][json_flag];
    if (i != size - 1){ buff << ",";}
  }
  return buff.str();
}

std::string GenerateStrGflag(const Json::Value& instances, const std::string& gflag_name,
                          const std::string& group, const std::string& json_flag) {
  int size = instances.size();
  std::stringstream buff;
  // Append Header
  buff << "--" << gflag_name << "=";
  // Append values
  for (int i = 0; i < size; i++) {
    buff << instances[i][group][json_flag].asString();
    if (i != size - 1){ buff << ",";}
  }
  return buff.str();
}

inline std::string BoolToString(bool val) { return val ? "true" : "false"; }

std::string GenerateBoolGflag(const Json::Value& instances,
                              const std::string& gflag_name,
                              const std::string& group,
                              const std::string& json_flag) {
  int size = instances.size();
  std::stringstream buff;
  // Append Header
  buff << "--" << gflag_name << "=";
  // Append values
  for (int i = 0; i < size; i++) {
    buff << BoolToString(instances[i][group][json_flag].asBool());
    if (i != size - 1) {
      buff << ",";
    }
  }
  return buff.str();
}

std::string GenerateIntGflagSubGroup(const Json::Value& instances,
                                     const std::string& gflag_name,
                                     const std::string& group,
                                     const std::string& subgroup,
                                     const std::string& json_flag) {
  int size = instances.size();
  std::stringstream buff;
  // Append Header
  buff << "--" << gflag_name << "=";
  // Append values
  for (int i = 0; i < size; i++) {
    buff << instances[i][group][subgroup][json_flag];
    if (i != size - 1){ buff << ",";}
  }
  return buff.str();
}

std::string GenerateStrGflagSubGroup(const Json::Value& instances,
                                  const std::string& gflag_name, const std::string& group,
                                  const std::string& subgroup, const std::string& json_flag) {
  int size = instances.size();
  std::stringstream buff;
  // Append Header
  buff << "--" << gflag_name << "=";
  // Append values
  for (int i = 0; i < size; i++) {
    buff << instances[i][group][subgroup][json_flag].asString();
    if (i != size - 1){ buff << ",";}
  }
  return buff.str();
}

std::string GenerateBoolGflagSubGroup(const Json::Value& instances,
                                      const std::string& gflag_name,
                                      const std::string& group,
                                      const std::string& subgroup,
                                      const std::string& json_flag) {
  int size = instances.size();
  std::stringstream buff;
  // Append Header
  buff << "--" << gflag_name << "=";
  // Append values
  for (int i = 0; i < size; i++) {
    buff << BoolToString(instances[i][group][subgroup][json_flag].asBool());
    if (i != size - 1) {
      buff << ",";
    }
  }
  return buff.str();
}

std::vector<std::string> MergeResults(std::vector<std::string> first_list,
                                      std::vector<std::string> scond_list) {
  std::vector<std::string> result;
  result.reserve(first_list.size() + scond_list.size());
  result.insert(result.begin(), first_list.begin(), first_list.end());
  result.insert(result.end(), scond_list.begin(), scond_list.end());
  return result;
}

}  // namespace cuttlefish
