/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/fetcher_configs.h"

#include <stddef.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "android-base/logging.h"

#include "cuttlefish/host/libs/config/fetcher_config.h"

namespace cuttlefish {

static constexpr std::string_view kFetcherConfigFile = "fetcher_config.json";

FetcherConfigs FetcherConfigs::ReadFromDirectories(
    const std::vector<std::string>& directories) {
  std::vector<FetcherConfig> configs;
  configs.reserve(directories.size());

  for (std::string_view directory : directories) {
    FetcherConfig& config = configs.emplace_back();

    std::string config_path = absl::StrCat(directory, "/", kFetcherConfigFile);
    if (!config.LoadFromFile(config_path)) {
      LOG(DEBUG) << "No valid fetcher_config in '" << config_path
                 << "', falling back to default";
    }
  }
  if (configs.empty()) {
    configs.emplace_back();
  }

  return FetcherConfigs(std::move(configs));
}

FetcherConfigs::FetcherConfigs(std::vector<FetcherConfig> configs)
    : fetcher_configs_(std::move(configs)) {}

const FetcherConfig& FetcherConfigs::ForInstance(size_t instance_index) const {
  if (instance_index < fetcher_configs_.size()) {
    return fetcher_configs_[instance_index];
  }
  return fetcher_configs_[0];
}

}  // namespace cuttlefish
