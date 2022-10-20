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

#pragma once
#include <json/json.h>
#include <iostream>

#include "common/libs/utils/result.h"
namespace cuttlefish {

Result<void> ValidateTypo(const Json::Value& root,
                          const std::map<std::string, Json::ValueType>& map);

void InitIntConfig(Json::Value& instances, const std::string& group,
                   const std::string& json_flag, int default_value);

void InitStringConfig(Json::Value& instances, const std::string& group,
                      const std::string& json_flag, const std::string& default_value);

void InitStringConfigSubGroup(Json::Value& instances, const std::string& group,
                              const std::string& subgroup, const std::string& json_flag,
                              const std::string& default_value);

std::string GenerateIntGflag(const Json::Value& instances, const std::string& gflag_name,
                          const std::string& group, const std::string& json_flag);

std::string GenerateStrGflag(const Json::Value& instances, const std::string& gflag_name,
                          const std::string& group, const std::string& json_flag);

std::string GenerateIntGflagSubGroup(const Json::Value& instances,
                                  const std::string& gflag_name, const std::string& group,
                                  const std::string& subgroup, const std::string& json_flag);

std::string GenerateStrGflagSubGroup(const Json::Value& instances,
                                  const std::string& gflag_name, const std::string& group,
                                  const std::string& subgroup, const std::string& json_flag);

std::vector<std::string> MergeResults(std::vector<std::string> first_list,
                                      std::vector<std::string> scond_list);

}  // namespace cuttlefish
