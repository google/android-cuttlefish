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

#include <cstddef>
#include <string>

#include "common/libs/utils/result.h"

namespace cuttlefish {

inline constexpr std::size_t kDefaultCacheSizeGb = 25;

Result<std::string> EmptyCache(const std::string& cache_directory);
Result<std::string> GetCacheInfo(const std::string& cache_directory,
                                 bool json_formatted);
Result<std::string> PruneCache(const std::string& cache_directory,
                               std::size_t allowed_size_GB);

}  // namespace cuttlefish
