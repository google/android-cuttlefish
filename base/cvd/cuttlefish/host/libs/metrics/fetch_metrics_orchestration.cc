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

#include "cuttlefish/host/libs/metrics/fetch_metrics_orchestration.h"

#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include "absl/log/log.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "cuttlefish/host/libs/metrics/enabled.h"
#include "cuttlefish/host/libs/metrics/event_type.h"
#include "cuttlefish/host/libs/metrics/metrics_conversion.h"
#include "cuttlefish/host/libs/metrics/metrics_orchestration.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

MetricsPaths GetFetchMetricsPaths(std::string_view target_directory) {
  return MetricsPaths{
      .metrics_directory = fmt::format("{}/{}", target_directory, "metrics"),
  };
}

Result<MetricsData> GatherFetchMetrics(const MetricsPaths& metrics_paths,
                                       EventType event_type,
                                       const FetchFlags& fetch_flags,
                                       const FetchResult& fetch_result) {
  MetricsData result = CF_EXPECT(GatherMetrics(metrics_paths, event_type));
  if (event_type == EventType::FetchStart) {
    result.fetch_start_metrics = CF_EXPECT(GetFetchStartMetrics(fetch_flags));
  }
  if (event_type == EventType::FetchComplete) {
    result.fetch_complete_metrics =
        CF_EXPECT(GetFetchCompleteMetrics(fetch_result));
  }
  return result;
}

void RunMetrics(const MetricsPaths& metrics_paths, EventType event_type,
                const FetchFlags& fetch_flags,
                const FetchResult& fetch_result) {
  ScopedLogger logger = CreateLogger(metrics_paths.metrics_directory);
  if (!FileExists(metrics_paths.metrics_directory)) {
    VLOG(0) << "Metrics directory does not exist, perhaps metrics were not "
               "initialized.";
    return;
  }

  Result<MetricsData> gather_result =
      GatherFetchMetrics(metrics_paths, event_type, fetch_flags, fetch_result);
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

void GatherFetchStartMetrics(const FetchFlags& fetch_flags) {
  const MetricsPaths metrics_paths =
      GetFetchMetricsPaths(fetch_flags.target_directory);
  Result<void> metrics_setup_result =
      SetUpMetrics(metrics_paths.metrics_directory);
  if (!metrics_setup_result.ok()) {
    VLOG(0) << fmt::format("Failed to initialize metrics.  Error: {}",
                           metrics_setup_result.error());
    return;
  }

  if (AreMetricsEnabled()) {
    LOG(INFO) << kMetricsEnabledNotice;
  }
  RunMetrics(metrics_paths, EventType::FetchStart, fetch_flags, {});
}

void GatherFetchCompleteMetrics(const std::string& target_directory,
                                const FetchResult& fetch_result) {
  const MetricsPaths metrics_paths = GetFetchMetricsPaths(target_directory);
  RunMetrics(metrics_paths, EventType::FetchComplete, {}, fetch_result);
}

void GatherFetchFailedMetrics(const std::string& target_directory) {
  const MetricsPaths metrics_paths = GetFetchMetricsPaths(target_directory);
  RunMetrics(metrics_paths, EventType::FetchFailed, {}, {});
}

}  // namespace cuttlefish
