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
#include <android-base/strings.h>
#include <json/json.h>

#include "common/libs/utils/json.h"
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
  for (auto& instance : instances) {
    if (!instance.isMember(group) || (!instance[group].isMember(subgroup)) ||
        (instance[group][subgroup].size() == 0)) {
      instance[group][subgroup][0][json_flag] = default_value;

    } else {
      // Check the whole array
      for (auto& subgroup_member : instance[group][subgroup]) {
        if (!subgroup_member.isMember(json_flag)) {
          subgroup_member[json_flag] = default_value;
        }
      }
    }
  }
}

std::string GenerateGflag(const std::string& gflag_name,
                          const std::vector<std::string>& values) {
  std::stringstream buff;
  buff << "--" << gflag_name << "=";
  buff << android::base::Join(values, ',');
  return buff.str();
}

Result<std::string> GenerateGflag(const Json::Value& instances,
                                  const std::string& gflag_name,
                                  const std::vector<std::string>& selectors) {
  auto values = CF_EXPECTF(GetArrayValues<std::string>(instances, selectors),
                           "Unable to get values for gflag \"{}\"", gflag_name);
  return GenerateGflag(gflag_name, values);
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
