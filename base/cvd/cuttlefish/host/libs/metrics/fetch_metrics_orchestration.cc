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

#include <optional>
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
#include "cuttlefish/host/libs/metrics/metrics_conversion.h"
#include "cuttlefish/host/libs/metrics/metrics_orchestration.h"
#include "cuttlefish/host/libs/metrics/notification.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

MetricsInput GetFetchMetricsInput(std::string_view target_directory) {
  return MetricsInput{
      .metrics_directory = fmt::format("{}/{}", target_directory, "metrics"),
  };
}

Result<MetricsData> GatherFetchMetrics(
    const MetricsInput& metrics_input,
    const std::optional<FetchInput>& fetch_input) {
  MetricsData result = CF_EXPECT(GatherMetrics(metrics_input));
  if (!fetch_input) {
    result.fetch_metrics = FetchFailedMetrics{};
  } else {
    result.fetch_metrics = CF_EXPECT(GetFetchMetrics(fetch_input.value()));
  }
  return result;
}

Result<void> RunMetrics(
    const MetricsInput& metrics_input,
    const std::optional<FetchInput>& fetch_input = std::nullopt) {
  ScopedLogger logger = CreateLogger(metrics_input.metrics_directory);
  CF_EXPECTF(FileExists(metrics_input.metrics_directory),
             "Metrics directory({}) does not exist, perhaps metrics were not "
             "initialized.",
             metrics_input.metrics_directory);

  const MetricsData metrics_data =
      CF_EXPECT(GatherFetchMetrics(metrics_input, fetch_input),
                "Failed to gather all metrics data for fetch metrics.");
  CF_EXPECT(metrics_data.fetch_metrics.has_value());

  const std::string event_type_label =
      ToEventLabel(metrics_data.fetch_metrics.value());
  CF_EXPECTF(OutputMetrics(event_type_label, metrics_input.metrics_directory,
                           metrics_data),
             "Failed to output metrics for {}.", event_type_label);
  return {};
}

}  // namespace

void GatherFetchStartMetrics(const FetchFlags& fetch_flags) {
  DisplayPrivacyNotice();
  const MetricsInput metrics_input =
      GetFetchMetricsInput(fetch_flags.target_directory);
  Result<void> metrics_setup_result =
      SetUpMetrics(metrics_input.metrics_directory);
  if (!metrics_setup_result.ok()) {
    VLOG(0) << fmt::format("Failed to initialize metrics.  Error: {}",
                           metrics_setup_result.error());
    return;
  }

  Result<void> run_metrics_result = RunMetrics(metrics_input, fetch_flags);
  if (!run_metrics_result.ok()) {
    VLOG(0) << fmt::format(
        "Failed during metrics gathering and (possible) outputting.  Error: {}",
        run_metrics_result.error());
    return;
  }
}

void GatherFetchCompleteMetrics(const std::string& target_directory,
                                const FetchResult& fetch_result) {
  const MetricsInput metrics_input = GetFetchMetricsInput(target_directory);
  Result<void> run_metrics_result = RunMetrics(metrics_input, fetch_result);
  if (!run_metrics_result.ok()) {
    VLOG(0) << fmt::format(
        "Failed during metrics gathering and (possible) outputting.  Error: {}",
        run_metrics_result.error());
    return;
  }
}

void GatherFetchFailedMetrics(const std::string& target_directory) {
  const MetricsInput metrics_input = GetFetchMetricsInput(target_directory);
  Result<void> run_metrics_result = RunMetrics(metrics_input);
  if (!run_metrics_result.ok()) {
    VLOG(0) << fmt::format(
        "Failed during metrics gathering and (possible) outputting.  Error: {}",
        run_metrics_result.error());
    return;
  }
}

}  // namespace cuttlefish
