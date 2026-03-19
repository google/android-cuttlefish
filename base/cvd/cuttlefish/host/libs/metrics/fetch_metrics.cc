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

#include "cuttlefish/host/libs/metrics/fetch_metrics.h"

#include <algorithm>

#include "cuttlefish/host/commands/cvd/fetch/builds.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<FetchStartMetrics> GetFetchStartMetrics(const FetchFlags& fetch_flags) {
  return FetchStartMetrics{
      .enable_local_caching = fetch_flags.build_api_flags.enable_caching,
      .dynamic_super_image_mixing =
          std::any_of(fetch_flags.vector_flags.dynamic_super_image.cbegin(),
                      fetch_flags.vector_flags.dynamic_super_image.cend(),
                      [](bool x) { return x; }),
  };
}

Result<FetchCompleteMetrics> GetFetchCompleteMetrics(
    const FetchResult& fetch_result) {
  std::vector<Builds> builds;
  std::transform(fetch_result.fetch_artifacts.cbegin(),
                 fetch_result.fetch_artifacts.cend(),
                 std::back_inserter(builds),
                 [](const FetchArtifacts& x) { return x.builds; });
  return FetchCompleteMetrics{
      .status_blocked =
          std::any_of(fetch_result.fetch_artifacts.cbegin(),
                      fetch_result.fetch_artifacts.cend(),
                      [](const FetchArtifacts& x) { return x.status_blocked; }),
      .fetch_size_bytes = fetch_result.fetch_size_bytes,
      .fetched_builds = builds,
  };
}

}  // namespace cuttlefish
