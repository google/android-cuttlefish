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

#include <chrono>
#include <string>

#include <android-base/logging.h>
#include <fmt/format.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/instances/instance_group_record.h"
#include "cuttlefish/host/commands/cvd/metrics/is_enabled.h"
#include "cuttlefish/host/commands/cvd/version/version.h"
#include "cuttlefish/host/libs/metrics/event_type.h"
#include "cuttlefish/host/libs/metrics/metrics_conversion.h"
#include "cuttlefish/host/libs/metrics/metrics_transmitter.h"
#include "cuttlefish/host/libs/metrics/metrics_writer.h"
#include "cuttlefish/host/libs/metrics/session_id.h"
#include "external_proto/clientanalytics.pb.h"

namespace cuttlefish {
namespace {

using wireless_android_play_playlog::LogRequest;

constexpr char kReadmeText[] =
    "The existence of records in this directory does"
    " not mean metrics are being transmitted, the data is always gathered and "
    "written out for debugging purposes.  To enable metrics transmission "
    "<TODO: chadreynolds - metrics transmission not connected, add triggering "
    "step"
    " when it does>";

std::chrono::milliseconds GetEpochTime() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now);
}

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

Result<void> GatherAndWriteMetrics(EventType event_type,
                                   const std::string& metrics_directory) {
  const std::string session_id =
      CF_EXPECT(ReadSessionIdFile(metrics_directory));
  const HostInfo host_metrics = GetHostInfo();
  const std::string cf_common_version = GetVersionIds().ToString();
  std::chrono::milliseconds now = GetEpochTime();
  // TODO: chadreynolds - gather the rest of the data (guest/flag information)
  const LogRequest log_request = ConstructLogRequest(
      event_type, host_metrics, session_id, cf_common_version, now);

  CF_EXPECT(WriteMetricsEvent(event_type, metrics_directory, log_request));
  if (kEnableCvdMetrics) {
    CF_EXPECT(TransmitMetricsEvent(log_request));
  }
  return {};
}

void RunMetrics(const std::string& metrics_directory, EventType event_type) {
  if (!FileExists(metrics_directory)) {
    LOG(INFO) << "Metrics directory does not exist, perhaps metrics were not "
                 "initialized.";
    return;
  }
  Result<void> event_result =
      GatherAndWriteMetrics(event_type, metrics_directory);
  if (!event_result.ok()) {
    LOG(INFO) << fmt::format("Failed to gather metrics for {}.  Error: {}",
                             EventTypeString(event_type), event_result.error());
  }
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
  if (kEnableCvdMetrics) {
    LOG(INFO) << "This will automatically send diagnostic information to "
                 "Google, such as crash reports and usage data from the host "
                 "machine managing the Android Virtual Device.";
  }
  RunMetrics(metrics_directory, EventType::DeviceInstantiation);
}

void GatherVmStartMetrics(const LocalInstanceGroup& instance_group) {
  const std::string metrics_directory =
      GetMetricsDirectoryFilepath(instance_group);
  RunMetrics(metrics_directory, EventType::DeviceBootStart);
}

void GatherVmBootCompleteMetrics(const LocalInstanceGroup& instance_group) {
  const std::string metrics_directory =
      GetMetricsDirectoryFilepath(instance_group);
  RunMetrics(metrics_directory, EventType::DeviceBootComplete);
}

void GatherVmStopMetrics(const LocalInstanceGroup& instance_group) {
  const std::string metrics_directory =
      GetMetricsDirectoryFilepath(instance_group);
  RunMetrics(metrics_directory, EventType::DeviceStop);
}

}  // namespace cuttlefish
