/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <stdint.h>

#include <variant>
#include <vector>

#include "cuttlefish/host/commands/cvd/fetch/builds.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct FetchStartMetrics {
  bool enable_local_caching = false;
  bool dynamic_super_image_mixing = false;
};

struct FetchCompleteMetrics {
  bool status_blocked = false;
  uint64_t fetch_size_bytes = 0;
  std::vector<Builds> fetched_builds;
};

struct FetchFailedMetrics {};

using FetchMetrics =
    std::variant<FetchStartMetrics, FetchCompleteMetrics, FetchFailedMetrics>;

Result<FetchMetrics> GetFetchStartMetrics(const FetchFlags& fetch_flags);
Result<FetchMetrics> GetFetchCompleteMetrics(const FetchResult& fetch_result);

}  // namespace cuttlefish
