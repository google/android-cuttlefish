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

#include <android-base/strings.h>
#include "absl/strings/str_split.h"
#include <fmt/format.h>
#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/version/version.h"
#include "cuttlefish/host/libs/metrics/enabled.h"
#include "cuttlefish/host/libs/metrics/event_type.h"
#include "cuttlefish/host/libs/metrics/flag_metrics.h"
#include "cuttlefish/host/libs/metrics/guest_metrics.h"
#include "cuttlefish/host/libs/metrics/metrics_conversion.h"
#include "cuttlefish/host/libs/metrics/metrics_transmitter.h"
#include "cuttlefish/host/libs/metrics/metrics_writer.h"
#include "cuttlefish/host/libs/metrics/session_id.h"
#include "cuttlefish/result/result.h"
#include "external_proto/cf_log.pb.h"

namespace cuttlefish {
namespace {

using logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent;

constexpr char kMetricsLogName[] = "metrics.log";

struct MetricsPaths {
  std::string metrics_directory;
  Guests guests;
};

std::chrono::milliseconds GetEpochTime() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now);
}

std::vector<GuestInfo> GetGuestInfos(
    const std::string& group_product_out,
    const std::vector<LocalInstance>& instances) {
  std::vector<GuestInfo> result;

  // Split always returns at least one element
  std::vector<std::string> product_out_paths =
      absl::StrSplit(group_product_out, ',');
  for (int i = 0; i < instances.size(); i++) {
    auto guest = GuestInfo{
        .instance_id = instances[i].id(),
    };
    if (product_out_paths.size() > i) {
      guest.product_out = product_out_paths[i];
    } else {  // pad with the first value
      guest.product_out = product_out_paths.front();
    }
    result.emplace_back(guest);
  }
  return result;
}

MetricsPaths GetMetricsPaths(const LocalInstanceGroup& instance_group) {
  return MetricsPaths{
      .metrics_directory = instance_group.MetricsDir(),
      .guests =
          Guests{
              .host_artifacts = instance_group.HostArtifactsPath(),
              .guest_infos = GetGuestInfos(instance_group.ProductOutPath(),
                                           instance_group.Instances()),
          },
  };
}

Result<void> SetUpMetrics(const std::string& metrics_directory) {
  CF_EXPECT(EnsureDirectoryExists(metrics_directory));
  CF_EXPECT(GenerateSessionIdFile(metrics_directory));
  return {};
}

Result<MetricsData> GatherMetrics(const MetricsPaths& metrics_paths,
                                  EventType event_type) {
  auto result = MetricsData{
      .event_type = event_type,
      .session_id =
          CF_EXPECT(ReadSessionIdFile(metrics_paths.metrics_directory)),
      .cf_common_version = GetVersionIds().ToString(),
      .now = GetEpochTime(),
      .host_metrics = GetHostInfo(),
      .guest_metrics = CF_EXPECT(GetGuestMetrics(metrics_paths.guests)),
      .flag_metrics =
          CF_EXPECT(GetFlagMetrics(metrics_paths.guests.guest_infos.size())),
  };

  CF_EXPECT_EQ(result.guest_metrics.size(), result.flag_metrics.size(),
               "The gathered guest and flag metrics vectors must be equal, as "
               "flags are per guest.");
  return result;
}

Result<void> OutputMetrics(EventType event_type,
                           const MetricsPaths& metrics_paths,
                           const MetricsData& metrics_data) {
  if (AreMetricsEnabled()) {
    const CuttlefishLogEvent cf_log_event =
        BuildCuttlefishLogEvent(metrics_data);
    CF_EXPECT(TransmitMetrics(kTransmitterPath, cf_log_event));
    CF_EXPECT(WriteMetricsEvent(event_type, metrics_paths.metrics_directory,
                                cf_log_event));
  }
  return {};
}

void RunMetrics(const MetricsPaths& metrics_paths, EventType event_type) {
  MetadataLevel metadata_level =
      isatty(0) ? MetadataLevel::ONLY_MESSAGE : MetadataLevel::FULL;
  ScopedLogger logger(SeverityTarget::FromFile(
                          fmt::format("{}/{}", metrics_paths.metrics_directory,
                                      kMetricsLogName),
                          metadata_level),
                      "");

  if (!FileExists(metrics_paths.metrics_directory)) {
    VLOG(0) << "Metrics directory does not exist, perhaps metrics were not "
               "initialized.";
    return;
  }

  Result<MetricsData> gather_result = GatherMetrics(metrics_paths, event_type);
  if (!gather_result.ok()) {
    VLOG(0) << fmt::format(
        "Failed to gather all metrics data for {}.  Error: {}",
        EventTypeString(event_type), gather_result.error());
    return;
  }

  Result<void> output_result =
      OutputMetrics(event_type, metrics_paths, *gather_result);
  if (!output_result.ok()) {
    VLOG(0) << fmt::format("Failed to output metrics for {}.  Error: {}",
                           EventTypeString(event_type), output_result.error());
  }
}

}  // namespace

void GatherVmInstantiationMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths = GetMetricsPaths(instance_group);
  Result<void> metrics_setup_result =
      SetUpMetrics(metrics_paths.metrics_directory);
  if (!metrics_setup_result.ok()) {
    VLOG(0) << fmt::format("Failed to initialize metrics.  Error: {}",
                           metrics_setup_result.error());
    return;
  }
  if (AreMetricsEnabled()) {
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

void GatherVmBootFailedMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths = GetMetricsPaths(instance_group);
  RunMetrics(metrics_paths, EventType::DeviceBootFailed);
}

void GatherVmStopMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths = GetMetricsPaths(instance_group);
  RunMetrics(metrics_paths, EventType::DeviceStop);
}

}  // namespace cuttlefish
