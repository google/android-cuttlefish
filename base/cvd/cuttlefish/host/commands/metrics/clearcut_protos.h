//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <vector>

#include "external_proto/cf_log.pb.h"
#include "external_proto/clientanalytics.pb.h"

namespace cuttlefish::metrics {

wireless_android_play_playlog::LogEvent BuildLogEvent(
    uint64_t now_ms,
    const logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent&
        cf_event);

wireless_android_play_playlog::LogRequest BuildLogRequest(
    uint64_t now_ms, wireless_android_play_playlog::LogEvent event);

wireless_android_play_playlog::LogRequest BuildLogRequest(
    uint64_t now_ms,
    std::vector<wireless_android_play_playlog::LogEvent> events);

}  // namespace cuttlefish::metrics
