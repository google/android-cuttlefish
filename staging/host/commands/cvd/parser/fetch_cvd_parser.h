/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

namespace cuttlefish {

struct FetchCvdInstanceConfig {
  bool should_fetch = false;
  // this subdirectory is relative to FetchCvdConfig::target_directory
  std::string target_subdirectory;
  std::optional<std::string> default_build;
  std::optional<std::string> system_build;
  std::optional<std::string> kernel_build;
};

struct FetchCvdConfig {
  std::string target_directory;
  std::optional<std::string> credential_source;
  std::vector<FetchCvdInstanceConfig> instances;
};

FetchCvdConfig ParseFetchCvdConfigs(Json::Value& root);

};  // namespace cuttlefish
