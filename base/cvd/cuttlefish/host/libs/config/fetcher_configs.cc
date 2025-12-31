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

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "android-base/file.h"

#include "cuttlefish/host/libs/config/fetcher_config.h"

namespace cuttlefish {

static constexpr std::string_view kFetcherConfigFile = "fetcher_config.json";

FetcherConfigs FetcherConfigs::ReadFromDirectories(
    absl::Span<const std::string> directories) {
  FetcherConfigs configs;

  configs.directories_.reserve(directories.size());

  for (const std::string& dir : directories) {
    std::string real;
    if (!android::base::Realpath(dir, &real)) {
      LOG(WARNING) << "Failed to resolve real path for '" << dir << "'";
      real = dir;
    }

    auto [it, inserted] =
        configs.directory_to_config_.emplace(real, FetcherConfig());

    configs.directories_.emplace_back(real);

    if (!inserted) {
      continue;
    }

    std::string path = absl::StrCat(real, "/", kFetcherConfigFile);

    if (!it->second.LoadFromFile(path)) {
      VLOG(0) << "No valid fetcher_config in '" << path
              << "', falling back to default";
    }
  }

  return configs;
}

const FetcherConfig& FetcherConfigs::ForInstance(size_t instance_index) const {
  // If there is no matching member in the map, either this FetcherConfig is a
  // moved-from instance without any members, or there is a mistake in
  // `FetcherConfigs::ReadFromDirectories`.
  static FetcherConfig* kFallback = new FetcherConfig();

  if (directories_.empty()) {
    return *kFallback;
  }

  instance_index = instance_index < directories_.size() ? instance_index : 0;

  auto it = directory_to_config_.find(directories_[instance_index]);
  return it == directory_to_config_.end() ? *kFallback : it->second;
}

}  // namespace cuttlefish
