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

#include <optional>
#include <variant>

#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/host/libs/metrics/gce_environment.h"
#include "cuttlefish/host/libs/metrics/github_environment.h"
#include "cuttlefish/host/libs/metrics/invoker.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct UnknownEnvironment {};

using Environment =
    std::variant<GceEnvironment, GitHubRepository, UnknownEnvironment>;

struct HostMetrics {
  HostInfo os;
  Invoker invoker;
  Environment environment;
};

Result<HostMetrics> GetHostMetrics();

}  // namespace cuttlefish
