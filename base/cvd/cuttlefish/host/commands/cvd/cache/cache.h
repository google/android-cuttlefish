/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/result/result.h"

namespace cuttlefish {

inline constexpr size_t kDefaultCacheSizeGb = 25;

struct PruneResult {
  size_t before;
  size_t after;
};

Result<void> EmptyCache(const std::string& cache_directory);
Result<size_t> GetCacheSize(const std::string& cache_directory);
Result<PruneResult> PruneCache(const std::string& cache_directory,
                               size_t allowed_size_GB);

}  // namespace cuttlefish
