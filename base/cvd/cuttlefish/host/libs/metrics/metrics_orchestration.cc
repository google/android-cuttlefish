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

#include "cuttlefish/host/libs/metrics/metrics_orchestration.h"

#include <string>

#include <android-base/logging.h>
#include <fmt/format.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/instances/instance_group_record.h"
#include "cuttlefish/host/libs/metrics/metrics_writer.h"
#include "cuttlefish/host/libs/metrics/session_id.h"

namespace cuttlefish {
namespace {

constexpr char kReadmeText[] =
    "The existence of records in this directory does"
    " not mean metrics are being transmitted, the data is always gathered and "
    "written out for debugging purposes.  To enable metrics transmission "
    "<TODO: chadreynolds - metrics transmission not connected, add triggering "
    "step"
    " when it does>";

std::string GetMetricsDirectoryFilepath(
    const LocalInstanceGroup& instance_group) {
  return instance_group.HomeDir() + "/metrics";
}

Result<void> SetUpMetrics(const std::string& metrics_directory) {
  CF_EXPECT(EnsureDirectoryExists(metrics_directory));
  CF_EXPECT(WriteNewFile(metrics_directory + "/README", kReadmeText));
  CF_EXPECT(GenerateSessionIdFile(metrics_directory));
  return {};
}

Result<void> GatherMetrics(const std::string& metrics_directory) {
  const std::string session_id =
      CF_EXPECT(ReadSessionIdFile(metrics_directory));
  HostInfo host_metrics = GetHostInfo();
  // TODO: chadreynolds - gather the rest of the data (guest/flag information)
  // TODO: chadreynolds - convert data to the proto representation
  CF_EXPECT(WriteMetricsEvent(metrics_directory, session_id, host_metrics));
  // TODO: chadreynolds - if <TBD> condition, transmit metrics event as well
  return {};
}

}  // namespace

// TODO: chadreynolds - add a metrics.log to capture these log messages
void GatherVmInstantiationMetrics(const LocalInstanceGroup& instance_group) {
  const std::string metrics_directory =
      GetMetricsDirectoryFilepath(instance_group);
  Result<void> metrics_setup_result = SetUpMetrics(metrics_directory);
  if (!metrics_setup_result.ok()) {
    LOG(ERROR) << fmt::format("Failed to initialize metrics.  Error: {}",
                              metrics_setup_result.error());
    return;
  }
  Result<void> event_result = GatherMetrics(metrics_directory);
  if (!event_result.ok()) {
    LOG(ERROR) << fmt::format(
        "Failed to gather device instantiation metrics.  Error: {}",
        event_result.error());
  }
}

}  // namespace cuttlefish
