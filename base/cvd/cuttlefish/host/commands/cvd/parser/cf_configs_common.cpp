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
#include <memory>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>
#include <json/json.h>

#include "common/libs/utils/base64.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"

using google::protobuf::Message;
using google::protobuf::util::JsonStringToMessage;

namespace cuttlefish {

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

Result<std::string> Base64EncodeGflag(
    const Json::Value& instances, const std::string& gflag_name,
    const std::vector<std::string>& selectors) {
  auto values =
      CF_EXPECTF(GetArrayValues<std::string>(instances, selectors),
                 "Unable to produce values for gflag \"{}\"", gflag_name);
  for (int i = 0; i < values.size(); i++) {
    std::string out;
    CF_EXPECT(EncodeBase64(values[i].c_str(), values[i].size(), &out));
    values[i] = out;
  }
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

Result<void> Validate(const Json::Value& value, Message& proto) {
  std::stringstream json_as_stringstream;
  json_as_stringstream << value;
  auto json_str = json_as_stringstream.str();

  auto status = JsonStringToMessage(json_str, &proto);
  return status.ok() ? Result<void>() : CF_ERR(status.ToString());
}

}  // namespace cuttlefish
