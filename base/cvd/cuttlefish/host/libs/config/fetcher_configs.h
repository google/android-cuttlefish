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

#include <string>
#include <vector>

#include "cuttlefish/host/libs/config/fetcher_config.h"

namespace cuttlefish {

class FetcherConfigs {
 public:
  static FetcherConfigs ReadFromDirectories(const std::vector<std::string>&);
  FetcherConfigs(FetcherConfigs&&) = default;
  ~FetcherConfigs() = default;

  size_t Size() const { return fetcher_configs_.size(); }

  const FetcherConfig& ForInstance(size_t instance_index) const;

 private:
  FetcherConfigs(std::vector<FetcherConfig> configs);
  std::vector<FetcherConfig> fetcher_configs_;
};

}  // namespace cuttlefish
