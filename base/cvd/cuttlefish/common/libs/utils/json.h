//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <json/json.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

Result<Json::Value> ParseJson(std::string_view input);

Result<Json::Value> LoadFromFile(SharedFD json_fd);
Result<Json::Value> LoadFromFile(const std::string& path_to_file);

template <typename T>
T As(const Json::Value& v);

template <>
inline int As(const Json::Value& v) {
  return v.asInt();
}

template <>
inline std::string As(const Json::Value& v) {
  return v.asString();
}

template <>
inline bool As(const Json::Value& v) {
  return v.asBool();
}

template <typename T>
Result<T> GetValue(const Json::Value& root,
                   const std::vector<std::string>& selectors) {
  const Json::Value* traversal = &root;
  for (const auto& selector : selectors) {
    CF_EXPECTF(traversal->isMember(selector),
               "JSON selector \"{}\" does not exist", selector);
    traversal = &(*traversal)[selector];
  }
  return As<T>(*traversal);
}

template <typename T>
Result<std::vector<T>> GetArrayValues(
    const Json::Value& array, const std::vector<std::string>& selectors) {
  std::vector<T> result;
  for (const auto& element : array) {
    result.emplace_back(CF_EXPECT(GetValue<T>(element, selectors)));
  }
  return result;
}

inline bool HasValue(const Json::Value& root,
                     const std::vector<std::string>& selectors) {
  const Json::Value* traversal = &root;
  for (const auto& selector : selectors) {
    if (!traversal->isMember(selector)) {
      return false;
    }
    traversal = &(*traversal)[selector];
  }
  return true;
}

}  // namespace cuttlefish
