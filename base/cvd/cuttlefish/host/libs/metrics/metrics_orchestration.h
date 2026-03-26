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

#pragma once

#include <string>
#include <string_view>

#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/libs/metrics/device_event_type.h"
#include "cuttlefish/host/libs/metrics/metrics_conversion.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct MetricsInput {
  std::string metrics_directory;
  Guests guests;
};

Result<void> SetUpMetrics(const std::string& metrics_directory);
ScopedLogger CreateLogger(std::string_view metrics_directory);
Result<MetricsData> GatherMetrics(const MetricsInput& metrics_input);
Result<void> OutputMetrics(DeviceEventType event_type,
                           std::string_view metrics_directory,
                           const MetricsData& metrics_data);

}  // namespace cuttlefish
