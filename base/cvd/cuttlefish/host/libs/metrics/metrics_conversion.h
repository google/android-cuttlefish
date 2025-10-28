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

#include <chrono>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/host/libs/metrics/event_type.h"
#include "cuttlefish/host/libs/metrics/guest_metrics.h"
#include "external_proto/clientanalytics.pb.h"

namespace cuttlefish {

struct MetricsData {
  EventType event_type;
  std::string session_id;
  std::string cf_common_version;
  std::chrono::milliseconds now;
  HostInfo host_metrics;
  std::vector<GuestInfo> guest_metrics;
};

wireless_android_play_playlog::LogRequest ConstructLogRequest(
    const MetricsData& metrics_data);

}  // namespace cuttlefish
