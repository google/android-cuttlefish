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

#include <string>
#include <type_traits>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>  // NOLINT(misc-include-cleaner): version difference
#include <google/protobuf/message.h>
#include <json/json.h>

#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

template <typename T>
std::string GenerateFlag(const std::string& name, const T& value) {
  return fmt::format("--{}={}", name, value);
}

template <typename T>
std::string GenerateVecFlag(const std::string& name, const T& collection) {
  return fmt::format("--{}={}", name, fmt::join(collection, ","));
}

template <typename T>
std::string GenerateInstanceFlag(
    const std::string& name,
    const cvd::config::EnvironmentSpecification& config, T callback) {
  if constexpr (std::is_invocable_v<T, const cvd::config::Instance&, size_t>) {
    std::vector<decltype(callback(config.instances()[0], 0))> values;
    for (size_t i = 0; i < config.instances_size(); ++i) {
      values.emplace_back(callback(config.instances(i), i));
    }
    return GenerateVecFlag(name, values);
  } else {
    std::vector<decltype(callback(config.instances()[0]))> values;
    for (size_t i = 0; i < config.instances_size(); ++i) {
      values.emplace_back(callback(config.instances(i)));
    }
    return GenerateVecFlag(name, values);
  }
}

template <typename T>
Result<std::string> ResultInstanceFlag(
    const std::string& name,
    const cvd::config::EnvironmentSpecification& config, T callback) {
  std::vector<std::decay_t<decltype(*callback(config.instances()[0]))>> values;
  for (const auto& instance : config.instances()) {
    values.emplace_back(CF_EXPECT(callback(instance)));
  }
  return GenerateVecFlag(name, values);
}

std::vector<std::string> MergeResults(std::vector<std::string> first_list,
                                      std::vector<std::string> scond_list);

void MergeTwoJsonObjs(Json::Value& dst, const Json::Value& src);

}  // namespace cuttlefish
