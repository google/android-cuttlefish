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

#include "cuttlefish/host/libs/metrics/conversion/metrics_data.h"

#include <chrono>

#include "google/protobuf/timestamp.pb.h"

#include "cuttlefish/host/libs/metrics/conversion/fetch_conversion.h"
#include "cuttlefish/host/libs/metrics/conversion/guest_conversion.h"
#include "cuttlefish/host/libs/metrics/conversion/host_conversion.h"
#include "cuttlefish/host/libs/metrics/fetch_metrics.h"
#include "cuttlefish/host/libs/metrics/guest_metrics.h"
#include "cuttlefish/host/libs/metrics/host_metrics.h"
#include "external_proto/cf_log.pb.h"
#include "external_proto/cf_metrics_event_v2.pb.h"

namespace cuttlefish {
namespace {

using google::protobuf::Timestamp;
using logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent;
using logs::proto::wireless::android::cuttlefish::events::MetricsEventV2;

}  // namespace

CuttlefishLogEvent BuildCuttlefishLogEvent(const MetricsData& metrics_data) {
  CuttlefishLogEvent cf_log_event;
  cf_log_event.set_device_type(CuttlefishLogEvent::CUTTLEFISH_DEVICE_TYPE_HOST);
  cf_log_event.set_session_id(metrics_data.session_id);
  cf_log_event.set_cuttlefish_version(metrics_data.cf_common_version);
  Timestamp& timestamp = *cf_log_event.mutable_timestamp_ms();
  timestamp.set_nanos((metrics_data.now.count() % 1000) * 1000000);
  timestamp.set_seconds(metrics_data.now.count() / 1000);

  MetricsEventV2& metrics_event = *cf_log_event.mutable_metrics_event_v2();

  PopulateCuttlefishHost(metrics_event, metrics_data.host_metrics);

  for (const GuestMetrics& guest_metric : metrics_data.guest_metrics) {
    PopulateCuttlefishGuest(metrics_event, guest_metric,
                            metrics_data.session_id);
  }

  if (metrics_data.fetch_metrics) {
    PopulateCuttlefishFetch(metrics_event, metrics_data.fetch_metrics.value());
  }

  return cf_log_event;
}

}  // namespace cuttlefish
