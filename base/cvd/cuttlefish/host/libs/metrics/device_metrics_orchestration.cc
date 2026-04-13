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
#include "cuttlefish/host/libs/metrics/device_event_type.h"
#include "cuttlefish/host/libs/metrics/enabled.h"
#include "cuttlefish/host/libs/metrics/guest_metrics.h"
#include "cuttlefish/host/libs/metrics/metrics_conversion.h"
#include "cuttlefish/host/libs/metrics/metrics_orchestration.h"
#include "cuttlefish/host/libs/metrics/parsed_flags.h"
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

Result<MetricsInput> GetInstanceGroupMetricsInput(
    const LocalInstanceGroup& instance_group,
    const DeviceEventType event_type) {
  return MetricsInput{
      .metrics_directory = instance_group.MetricsDir(),
      .guests =
          Guests{
              .host_artifacts = instance_group.HostArtifactsPath(),
              .event_type = event_type,
              .parsed_flags = CF_EXPECT(GetParsedFlags()),
              .guest_infos = GetGuestInfos(instance_group.ProductOutPath(),
                                           instance_group.Instances()),
          },
  };
}

Result<void> RunMetrics(const MetricsInput& metrics_input) {
  ScopedLogger logger = CreateLogger(metrics_input.metrics_directory);
  CF_EXPECTF(FileExists(metrics_input.metrics_directory),
             "Metrics directory({}) does not exist.  Perhaps metrics were not "
             "initialized?",
             metrics_input.metrics_directory);
  CF_EXPECT(metrics_input.guests.has_value(),
            "Guest information not populated for device metrics event, cannot "
            "gather metrics data.");

  const MetricsData metrics_data = CF_EXPECTF(
      GatherMetrics(metrics_input), "Failed to gather all metrics data for {}.",
      DeviceEventTypeString(metrics_input.guests->event_type));
  CF_EXPECTF(
      OutputMetrics(DeviceEventTypeString(metrics_input.guests->event_type),
                    metrics_input.metrics_directory, metrics_data),
      "Failed to output metrics for {}.",
      DeviceEventTypeString(metrics_input.guests->event_type));
  return {};
}

}  // namespace

void GatherVmInstantiationMetrics(const LocalInstanceGroup& instance_group) {
  Result<MetricsInput> metrics_input_result = GetInstanceGroupMetricsInput(
      instance_group, DeviceEventType::DeviceInstantiation);
  if (!metrics_input_result.ok()) {
    VLOG(0) << fmt::format("Failed to initialize metrics.  Error: {}",
                           metrics_input_result.error());
    return;
  }
  Result<void> metrics_setup_result =
      SetUpMetrics(metrics_input_result->metrics_directory);
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
  Result<void> run_metrics_result = RunMetrics(*metrics_input_result);
  if (!run_metrics_result.ok()) {
    VLOG(0) << fmt::format(
        "Failed during metrics gathering and (possible) outputting.  Error: {}",
        run_metrics_result.error());
    return;
  }
}

void GatherVmStartMetrics(const LocalInstanceGroup& instance_group) {
  Result<MetricsInput> metrics_input_result = GetInstanceGroupMetricsInput(
      instance_group, DeviceEventType::DeviceBootStart);
  if (!metrics_input_result.ok()) {
    VLOG(0) << fmt::format("Failed to initialize metrics.  Error: {}",
                           metrics_input_result.error());
    return;
  }
  Result<void> run_metrics_result = RunMetrics(*metrics_input_result);
  if (!run_metrics_result.ok()) {
    VLOG(0) << fmt::format(
        "Failed during metrics gathering and (possible) outputting.  Error: {}",
        run_metrics_result.error());
    return;
  }
}

void GatherVmBootCompleteMetrics(const LocalInstanceGroup& instance_group) {
  Result<MetricsInput> metrics_input_result = GetInstanceGroupMetricsInput(
      instance_group, DeviceEventType::DeviceBootComplete);
  if (!metrics_input_result.ok()) {
    VLOG(0) << fmt::format("Failed to initialize metrics.  Error: {}",
                           metrics_input_result.error());
    return;
  }
  Result<void> run_metrics_result = RunMetrics(*metrics_input_result);
  if (!run_metrics_result.ok()) {
    VLOG(0) << fmt::format(
        "Failed during metrics gathering and (possible) outputting.  Error: {}",
        run_metrics_result.error());
    return;
  }
}

void GatherVmBootFailedMetrics(const LocalInstanceGroup& instance_group) {
  Result<MetricsInput> metrics_input_result = GetInstanceGroupMetricsInput(
      instance_group, DeviceEventType::DeviceBootFailed);
  if (!metrics_input_result.ok()) {
    VLOG(0) << fmt::format("Failed to initialize metrics.  Error: {}",
                           metrics_input_result.error());
    return;
  }
  Result<void> run_metrics_result = RunMetrics(*metrics_input_result);
  if (!run_metrics_result.ok()) {
    VLOG(0) << fmt::format(
        "Failed during metrics gathering and (possible) outputting.  Error: {}",
        run_metrics_result.error());
    return;
  }
}

void GatherVmStopMetrics(const LocalInstanceGroup& instance_group) {
  Result<MetricsInput> metrics_input_result =
      GetInstanceGroupMetricsInput(instance_group, DeviceEventType::DeviceStop);
  if (!metrics_input_result.ok()) {
    VLOG(0) << fmt::format("Failed to initialize metrics.  Error: {}",
                           metrics_input_result.error());
    return;
  }
  Result<void> run_metrics_result = RunMetrics(*metrics_input_result);
  if (!run_metrics_result.ok()) {
    VLOG(0) << fmt::format(
        "Failed during metrics gathering and (possible) outputting.  Error: {}",
        run_metrics_result.error());
    return;
  }
}

}  // namespace cuttlefish
