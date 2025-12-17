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
#pragma once

#include <stddef.h>

#include <map>
#include <string>
#include <vector>

#include "absl/types/span.h"

#include "cuttlefish/host/libs/config/fetcher_config.h"

namespace cuttlefish {

class FetcherConfigs {
 public:
  static FetcherConfigs ReadFromDirectories(absl::Span<const std::string>);

  FetcherConfigs(FetcherConfigs&&) = default;
  ~FetcherConfigs() = default;

  size_t Size() const { return directories_.size(); }

  const FetcherConfig& ForInstance(size_t instance_index) const;

 private:
  FetcherConfigs() = default;

  std::vector<std::string> directories_;
  std::map<std::string, FetcherConfig> directory_to_config_;
};

}  // namespace cuttlefish
