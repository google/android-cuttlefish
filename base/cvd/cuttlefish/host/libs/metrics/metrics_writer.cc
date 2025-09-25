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

#include "cuttlefish/host/libs/metrics/metrics_writer.h"

#include <chrono>
#include <string>

#include <fmt/format.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/random.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {
namespace {

std::string GenerateFilenameSuffix() {
  const std::string nums("0123456789");
  return GenerateRandomString(nums, 10);
}

}  // namespace

Result<void> WriteMetricsEvent(const std::string& metrics_directory,
                               const HostInfo& host_metrics) {
  const std::string event_filepath =
      fmt::format("{}/vm-instantiation_{}_{}", metrics_directory,
                  std::chrono::system_clock::now(), GenerateFilenameSuffix());
  // TODO: chadreynolds - convert (what will be a proto) to text
  CF_EXPECT(WriteNewFile(event_filepath, host_metrics.to_string()));
  return {};
}

}  // namespace cuttlefish
