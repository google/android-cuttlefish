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
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/cvd/version/version.h"
#include "cuttlefish/host/libs/metrics/enabled.h"
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

std::chrono::milliseconds GetEpochTime() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now);
}

}  // namespace

Result<void> SetUpMetrics(const std::string& metrics_directory) {
  CF_EXPECT(EnsureDirectoryExists(metrics_directory));
  CF_EXPECT(GenerateSessionIdFile(metrics_directory));
  return {};
}

ScopedLogger CreateLogger(std::string_view metrics_directory) {
  MetadataLevel metadata_level =
      isatty(0) ? MetadataLevel::ONLY_MESSAGE : MetadataLevel::FULL;
  return ScopedLogger(
      SeverityTarget::FromFile(
          fmt::format("{}/{}", metrics_directory, kMetricsLogName),
          metadata_level),
      "");
}

Result<MetricsData> GatherMetrics(const MetricsInput& metrics_input) {
  auto result = MetricsData{
      .session_id =
          CF_EXPECT(ReadSessionIdFile(metrics_input.metrics_directory)),
      .cf_common_version = GetVersionIds().ToString(),
      .now = GetEpochTime(),
      .host_metrics = GetHostInfo(),
  };

  if (metrics_input.guests) {
    result.guest_metrics =
        CF_EXPECT(GetGuestMetrics(metrics_input.guests.value()));
  }

  return result;
}

Result<void> OutputMetrics(std::string_view event_type_label,
                           std::string_view metrics_directory,
                           const MetricsData& metrics_data) {
  if (AreMetricsEnabled()) {
    const CuttlefishLogEvent cf_log_event =
        BuildCuttlefishLogEvent(metrics_data);
    CF_EXPECT(TransmitMetrics(kTransmitterPath, cf_log_event));
    CF_EXPECT(
        WriteMetricsEvent(event_type_label, metrics_directory, cf_log_event));
  }
  return {};
}

}  // namespace cuttlefish
