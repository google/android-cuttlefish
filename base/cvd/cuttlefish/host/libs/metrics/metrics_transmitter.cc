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

#include "cuttlefish/host/libs/metrics/metrics_transmitter.h"

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/metrics/send.h"
#include "cuttlefish/host/libs/metrics/metrics_defs.h"
#include "external_proto/clientanalytics.pb.h"

namespace cuttlefish {

Result<void> TransmitMetricsEvent(
    const wireless_android_play_playlog::LogRequest& log_request) {
  int reporting_outcome = metrics::PostRequest(log_request.SerializeAsString(),
                                               metrics::ClearcutServer::kProd);
  CF_EXPECTF(reporting_outcome != MetricsExitCodes::kSuccess,
             "Issue reporting metrics: {}", reporting_outcome);
  return {};
}

}  // namespace cuttlefish