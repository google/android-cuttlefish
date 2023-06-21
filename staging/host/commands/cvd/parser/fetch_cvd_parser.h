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

#include <string>
#include <vector>

#include <json/json.h>

namespace cuttlefish {

struct FetchCvdDeviceConfigs {
  bool use_fetch_artifact;
  std::string default_build;
  std::string system_build;
  std::string kernel_build;
  std::string host_artifacts_dir;
};

struct FetchCvdConfigs {
  std::string credential;
  std::vector<FetchCvdDeviceConfigs> instances;
};

FetchCvdConfigs ParseFetchCvdConfigs(Json::Value& root);

};  // namespace cuttlefish
