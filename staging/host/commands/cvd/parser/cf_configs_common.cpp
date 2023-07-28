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

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <json/json.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {

/**
 * Validate Json data Name and type
 */
Result<void> ValidateTypo(const Json::Value& root,
                          const std::map<std::string, Json::ValueType>& map) {
  for (const std::string& flag : root.getMemberNames()) {
    CF_EXPECT(map.count(flag) != 0,
              "Invalid input flag name:- " << flag << " not recognized");
    CF_EXPECT(root[flag].isConvertibleTo(map.at(flag)),
              "Invalid flag type" << flag);
  }
  return {};
}

void InitIntConfigSubGroupVector(Json::Value& instances,
                                 const std::string& group,
                                 const std::string& subgroup,
                                 const std::string& json_flag,
                                 int default_value) {
  // Allocate and initialize with default values
  for (int i = 0; i < instances.size(); i++) {
    if (!instances[i].isMember(group) ||
        (!instances[i][group].isMember(subgroup)) ||
        (instances[i][group][subgroup].size() == 0)) {
      instances[i][group][subgroup][0][json_flag] = default_value;

    } else {
      // Check the whole array
      int vector_size = instances[i][group][subgroup].size();
      for (int j = 0; j < vector_size; j++) {
        if (!instances[i][group][subgroup][j].isMember(json_flag)) {
          instances[i][group][subgroup][j][json_flag] = default_value;
        }
      }
    }
  }
}

Result<std::string> GenerateGflag(const Json::Value& instances,
                                  const std::string& gflag_name,
                                  const std::vector<std::string>& selectors) {
  std::stringstream buff;
  buff << "--" << gflag_name << "=";

  int size = instances.size();
  for (int i = 0; i < size; i++) {
    const Json::Value* traversal = &instances[i];
    for (const auto& selector : selectors) {
      CF_EXPECTF(traversal->isMember(selector),
                 "JSON selector \"{}\" does not exist when trying to create "
                 "gflag \"{}\"",
                 selector, gflag_name);
      traversal = &(*traversal)[selector];
    }
    buff << traversal->asString();
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

/**
 * @brief This function merges two json objects and override json tree in dst
 * with src json keys
 *
 * @param dst : destination json object tree(modified in place)
 * @param src : input json object tree to be merged
 */
void MergeTwoJsonObjs(Json::Value& dst, const Json::Value& src) {
  // Merge all members of src into dst
  for (const auto& key : src.getMemberNames()) {
    if (src[key].type() == Json::arrayValue) {
      for (int i = 0; i < src[key].size(); i++) {
        MergeTwoJsonObjs(dst[key][i], src[key][i]);
      }
    } else if (src[key].type() == Json::objectValue) {
      MergeTwoJsonObjs(dst[key], src[key]);
    } else {
      dst[key] = src[key];
    }
  }
}

}  // namespace cuttlefish
