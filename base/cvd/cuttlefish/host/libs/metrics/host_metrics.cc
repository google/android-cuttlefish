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

#include "cuttlefish/host/libs/metrics/host_metrics.h"

#include <optional>

#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/host/libs/metrics/gce_environment.h"
#include "cuttlefish/host/libs/metrics/github_environment.h"
#include "cuttlefish/host/libs/metrics/invoker.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<std::optional<Environment>> GetEnvironment() {
  const std::optional<GitHubRepository> github_environment =
      DetectGitHubRepository();
  if (github_environment) {
    return github_environment;
  }

  const std::optional gce_environment = CF_EXPECT(DetectGceEnvironment());
  if (gce_environment) {
    return gce_environment;
  }

  return std::nullopt;
}

}  // namespace

Result<HostMetrics> GetHostMetrics() {
  return HostMetrics{
      .os = GetHostInfo(),
      .invoker = GetInvoker(),
      .environment = CF_EXPECT(GetEnvironment()),
  };
}

}  // namespace cuttlefish
