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
#include <random>
#include <string>

#include <fmt/format.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {
namespace {

std::string GenerateRandomString() {
  std::string str("0123456789");
  std::random_device rd;
  std::mt19937 generator(rd());
  std::shuffle(str.begin(), str.end(), generator);
  return str;
}

}  // namespace

// TODO CJR: depending on the eventual structure of the messages, may want
// separate methods for writing
Result<void> WriteMetricsEvent(const std::string& metrics_directory,
                               const HostMetrics& host_metrics) {
  const std::string event_filepath =
      fmt::format("{}/vm-instantiation_{}_{}", metrics_directory,
                  std::chrono::system_clock::now(), GenerateRandomString());
  // TODO: chadreynolds - convert (what will be a proto) to text
  CF_EXPECT(WriteNewFile(event_filepath, host_metrics.release + "\n"));
  return {};
}

}  // namespace cuttlefish
