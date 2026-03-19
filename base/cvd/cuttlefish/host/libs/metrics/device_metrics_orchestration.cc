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

#include "cuttlefish/host/libs/metrics/device_metrics_orchestration.h"

#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include "absl/log/log.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/libs/metrics/enabled.h"
#include "cuttlefish/host/libs/metrics/event_type.h"
#include "cuttlefish/host/libs/metrics/guest_metrics.h"
#include "cuttlefish/host/libs/metrics/metrics_conversion.h"
#include "cuttlefish/host/libs/metrics/metrics_orchestration.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

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

MetricsPaths GetInstanceGroupMetricsPaths(
    const LocalInstanceGroup& instance_group) {
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

void RunMetrics(const MetricsPaths& metrics_paths, EventType event_type) {
  ScopedLogger logger = CreateLogger(metrics_paths.metrics_directory);
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

  Result<void> output_result = OutputMetrics(
      event_type, metrics_paths.metrics_directory, *gather_result);
  if (!output_result.ok()) {
    VLOG(0) << fmt::format("Failed to output metrics for {}.  Error: {}",
                           EventTypeString(event_type), output_result.error());
  }
}

}  // namespace

void GatherVmInstantiationMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths =
      GetInstanceGroupMetricsPaths(instance_group);
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
  const MetricsPaths metrics_paths =
      GetInstanceGroupMetricsPaths(instance_group);
  RunMetrics(metrics_paths, EventType::DeviceBootStart);
}

void GatherVmBootCompleteMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths =
      GetInstanceGroupMetricsPaths(instance_group);
  RunMetrics(metrics_paths, EventType::DeviceBootComplete);
}

void GatherVmBootFailedMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths =
      GetInstanceGroupMetricsPaths(instance_group);
  RunMetrics(metrics_paths, EventType::DeviceBootFailed);
}

void GatherVmStopMetrics(const LocalInstanceGroup& instance_group) {
  const MetricsPaths metrics_paths =
      GetInstanceGroupMetricsPaths(instance_group);
  RunMetrics(metrics_paths, EventType::DeviceStop);
}

}  // namespace cuttlefish
