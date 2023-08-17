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

#include "host/commands/cvd/parser/load_configs_parser.h"

#include <cstdio>
#include <fstream>
#include <string>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <gflags/gflags.h>
#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/cf_flags_validator.h"
#include "host/commands/cvd/parser/fetch_cvd_parser.h"
#include "host/commands/cvd/parser/launch_cvd_parser.h"

namespace cuttlefish {
namespace {

Result<void> ValidateArgFormat(const std::string& str) {
  auto equalsPos = str.find('=');
  CF_EXPECT(equalsPos != std::string::npos,
            "equal value is not provided in the argument");
  std::string prefix = str.substr(0, equalsPos);
  CF_EXPECT(!prefix.empty(), "argument value should not be empty");
  CF_EXPECT(prefix.find('.') != std::string::npos,
            "argument value must be dot separated");
  CF_EXPECT(prefix[0] != '.', "argument value should not start with a dot");
  CF_EXPECT(prefix.find("..") == std::string::npos,
            "argument value should not contain two consecutive dots");
  CF_EXPECT(prefix.back() != '.', "argument value should not end with a dot");
  return {};
}

Result<void> ValidateArgsFormat(const std::vector<std::string>& strings) {
  for (const auto& str : strings) {
    CF_EXPECT(ValidateArgFormat(str),
              "Invalid  argument format. " << str << " Please use arg=value");
  }
  return {};
}

// TODO(moelsherif): expand this enum in the future to support more types (
// double , float , etc) if neeeded
enum ArgValueType { UINTEGER, BOOLEAN, TEXT };

bool IsUnsignedInteger(const std::string& str) {
  return !str.empty() && std::all_of(str.begin(), str.end(),
                                     [](char c) { return std::isdigit(c); });
}

ArgValueType GetArgValueType(const std::string& str) {
  if (IsUnsignedInteger(str)) {
    return UINTEGER;
  }

  if (str == "true" || str == "false") {
    return BOOLEAN;
  }

  // Otherwise, treat the string as text
  return TEXT;
}

Json::Value ConvertArgToJson(const std::string& key,
                             const std::string& leafValue) {
  std::stack<std::string> levels;
  std::stringstream ks(key);
  std::string token;
  while (std::getline(ks, token, '.')) {
    levels.push(token);
  }

  // assign the leaf value based on the type of input value
  Json::Value leaf;
  if (GetArgValueType(leafValue) == UINTEGER) {
    std::uint32_t leaf_val{};
    if (!android::base::ParseUint(leafValue, &leaf_val)) {
      LOG(ERROR) << "Failed to parse unsigned integer " << leafValue;
      return Json::Value::null;
    };
    leaf = leaf_val;
  } else if (GetArgValueType(leafValue) == BOOLEAN) {
    leaf = (leafValue == "true");
  } else {
    leaf = leafValue;
  }

  while (!levels.empty()) {
    Json::Value curr;
    std::string index = levels.top();

    if (GetArgValueType(index) == UINTEGER) {
      std::uint32_t index_val{};
      if (!android::base::ParseUint(index, &index_val)) {
        LOG(ERROR) << "Failed to parse unsigned integer " << index;
        return Json::Value::null;
      }
      curr[index_val] = leaf;
    } else {
      curr[index] = leaf;
    }

    leaf = curr;
    levels.pop();
  }

  return leaf;
}

Json::Value ParseArgsToJson(const std::vector<std::string>& strings) {
  Json::Value jsonValue;
  for (const auto& str : strings) {
    std::string key;
    std::string value;
    size_t equals_pos = str.find('=');
    if (equals_pos != std::string::npos) {
      key = str.substr(0, equals_pos);
      value = str.substr(equals_pos + 1);
    } else {
      key = str;
      value.clear();
      LOG(WARNING) << "No value provided for key " << key;
      return Json::Value::null;
    }
    MergeTwoJsonObjs(jsonValue, ConvertArgToJson(key, value));
  }
  return jsonValue;
}

}  // namespace

Result<Json::Value> ParseJsonFile(const std::string& file_path) {
  CF_EXPECT(FileExists(file_path), "provided File to cvd load does not exist");

  std::string file_content;
  using android::base::ReadFileToString;
  CF_EXPECT(ReadFileToString(file_path.c_str(), &file_content,
                             /* follow_symlinks */ true),
            "Failed to read file");
  auto root = CF_EXPECT(ParseJson(file_content), "Failed parsing JSON file");
  return root;
}

Result<Json::Value> GetOverridedJsonConfig(
    const std::string& config_path,
    const std::vector<std::string>& override_flags) {
  Json::Value result = CF_EXPECT(ParseJsonFile(config_path),
                                 "Parsing json config input file failed");

  if (override_flags.size() > 0) {
    CF_EXPECT(ValidateArgsFormat(override_flags),
              "override parameters are not in the correct format");
    auto args_tree = ParseArgsToJson(override_flags);
    MergeTwoJsonObjs(result, args_tree);
  }

  return result;
}

Result<CvdFlags> ParseCvdConfigs(Json::Value& root) {
  CF_EXPECT(ValidateCfConfigs(root), "Loaded Json validation failed");
  return CvdFlags{.launch_cvd_flags = CF_EXPECT(ParseLaunchCvdConfigs(root)),
                  .fetch_cvd_flags = CF_EXPECT(ParseFetchCvdConfigs(root))};
}

}  // namespace cuttlefish
