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

#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fmt/format.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/cvd/instances/instance_group_record.h"
#include "cuttlefish/host/commands/cvd/metrics/is_enabled.h"
#include "cuttlefish/host/commands/cvd/version/version.h"
#include "cuttlefish/host/libs/metrics/event_type.h"
#include "cuttlefish/host/libs/metrics/guest_metrics.h"
#include "cuttlefish/host/libs/metrics/metrics_conversion.h"
#include "cuttlefish/host/libs/metrics/metrics_transmitter.h"
#include "cuttlefish/host/libs/metrics/metrics_writer.h"
#include "cuttlefish/host/libs/metrics/session_id.h"
#include "external_proto/clientanalytics.pb.h"

namespace cuttlefish {
namespace {

using wireless_android_play_playlog::LogRequest;

constexpr char kMetricsLogName[] = "metrics.log";

constexpr char kReadmeText[] =
    "The existence of records in this directory does"
    " not mean metrics are being transmitted, the data is always gathered and "
    "written out for debugging purposes.  To enable metrics transmission "
    "<TODO: chadreynolds - metrics transmission not connected, add triggering "
    "step"
    " when it does>";

struct MetricsPaths {
  std::string metrics_directory;
  GuestPaths guest_paths;
};

std::chrono::milliseconds GetEpochTime() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now);
}

// use as many ProductOut values are available, then pad with the first value up
// to the number of instances
std::vector<std::string> GetProductOutPaths(
    const std::string& group_product_out, const int instance_count) {
  std::vector<std::string> result =
      android::base::Split(group_product_out, ",");
  if (!result.empty() && result.size() < instance_count) {
    for (int i = result.size(); i < instance_count; i++) {
      result.emplace_back(result.front());
    }
  }
  return result;
}

MetricsPaths GetMetricsPaths(const LocalInstanceGroup& instance_group) {
  return MetricsPaths{
      .metrics_directory = instance_group.HomeDir() + "/metrics",
      .guest_paths =
          GuestPaths{
              .host_artifacts = instance_group.HostArtifactsPath(),
              .artifacts =
                  GetProductOutPaths(instance_group.ProductOutPath(),
                                     instance_group.Instances().size()),
          },
  };
}

Result<void> SetUpMetrics(const std::string& metrics_directory) {
  CF_EXPECT(EnsureDirectoryExists(metrics_directory));
  CF_EXPECT(WriteNewFile(metrics_directory + "/README", kReadmeText));
  CF_EXPECT(GenerateSessionIdFile(metrics_directory));
  return {};
}

Result<MetricsData> GatherMetrics(const MetricsPaths& metrics_paths,
                                  EventType event_type) {
  // TODO: chadreynolds - gather the rest of the data (guest/flag information)
  return MetricsData{
      .event_type = event_type,
      .session_id =
          CF_EXPECT(ReadSessionIdFile(metrics_paths.metrics_directory)),
      .cf_common_version = GetVersionIds().ToString(),
      .now = GetEpochTime(),
      .host_metrics = GetHostInfo(),
      .guest_metrics = CF_EXPECT(GetGuestInfo(metrics_paths.guest_paths)),
  };
}

Result<void> OutputMetrics(EventType event_type,
                           const std::string& metrics_directory,
                           const MetricsData& metrics_data) {
  const LogRequest log_request = ConstructLogRequest(metrics_data);
  CF_EXPECT(WriteMetricsEvent(event_type, metrics_directory, log_request));
  if (kEnableCvdMetrics) {
    CF_EXPECT(TransmitMetricsEvent(log_request));
  }
  return {};
}

void RunMetrics(const MetricsPaths& metrics_paths, EventType event_type) {
  MetadataLevel metadata_level =
      isatty(0) ? MetadataLevel::ONLY_MESSAGE : MetadataLevel::FULL;
  ScopedTeeLogger logger(LogToStderrAndFiles(
      {fmt::format("{}/{}", metrics_paths.metrics_directory, kMetricsLogName)},
      "", metadata_level));

  if (!FileExists(metrics_paths.metrics_directory)) {
    LOG(DEBUG) << "Metrics directory does not exist, perhaps metrics were not "
                  "initialized.";
    return;
  }

  Result<MetricsData> gather_result = GatherMetrics(metrics_paths, event_type);
  if (!gather_result.ok()) {
    LOG(DEBUG) << fmt::format(
        "Failed to gather all metrics data for {}.  Error: {}",
        EventTypeString(event_type), gather_result.error().FormatForEnv());
    return;
  }

  Result<void> output_result = OutputMetrics(
      event_type, metrics_paths.metrics_directory, *gather_result);
  if (!output_result.ok()) {
    LOG(DEBUG) << fmt::format("Failed to output metrics for {}.  Error: {}",
                              EventTypeString(event_type),
                              output_result.error().FormatForEnv());
  }
}

}  // namespace

void GatherVmInstantiationMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths = GetMetricsPaths(instance_group);
  Result<void> metrics_setup_result =
      SetUpMetrics(metrics_paths.metrics_directory);
  if (!metrics_setup_result.ok()) {
    LOG(DEBUG) << fmt::format("Failed to initialize metrics.  Error: {}",
                              metrics_setup_result.error().FormatForEnv());
    return;
  }
  if (kEnableCvdMetrics) {
    LOG(INFO) << "This will automatically send diagnostic information to "
                 "Google, such as crash reports and usage data from the host "
                 "machine managing the Android Virtual Device.";
  }
  RunMetrics(metrics_paths, EventType::DeviceInstantiation);
}

void GatherVmStartMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths = GetMetricsPaths(instance_group);
  RunMetrics(metrics_paths, EventType::DeviceBootStart);
}

void GatherVmBootCompleteMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths = GetMetricsPaths(instance_group);
  RunMetrics(metrics_paths, EventType::DeviceBootComplete);
}

void GatherVmStopMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths = GetMetricsPaths(instance_group);
  RunMetrics(metrics_paths, EventType::DeviceStop);
}

}  // namespace cuttlefish
