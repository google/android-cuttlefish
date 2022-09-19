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

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <gflags/gflags.h>
#include <string>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/metrics/events.h"
#include "host/commands/metrics/metrics_defs.h"
#include "host/commands/metrics/proto/cf_metrics_proto.h"
#include "host/commands/metrics/utils.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"
#include "shared/api_level.h"

using cuttlefish::MetricsExitCodes;

const std::string kLogSourceStr = "CUTTLEFISH_METRICS";
const int kLogSourceId = 1753;
const int kCppClientType =
    19;  // C++ native client type (clientanalytics.proto)

namespace cuttlefish {

Clearcut::Clearcut() {}

Clearcut::~Clearcut() {}

std::unique_ptr<cuttlefish::CuttlefishLogEvent> buildCFLogEvent(
    uint64_t now_ms, cuttlefish::CuttlefishLogEvent::DeviceType device_type) {
  uint64_t now_s = now_ms / 1000;
  uint64_t now_ns = (now_ms % 1000) * 1000000;

  // "cfEvent" is the top level CuttlefishLogEvent
  auto cfEvent = std::make_unique<cuttlefish::CuttlefishLogEvent>();
  cfEvent->set_device_type(device_type);
  cfEvent->set_session_id(metrics::sessionId(now_ms));
  if (metrics::cfVersion() != "") {
    cfEvent->set_cuttlefish_version(metrics::cfVersion());
  }
  Timestamp* timestamp = cfEvent->mutable_timestamp_ms();
  timestamp->set_seconds(now_s);
  timestamp->set_nanos(now_ns);
  return cfEvent;
}

void buildCFMetricsEvent(uint64_t now_ms,
                         cuttlefish::CuttlefishLogEvent* cfEvent,
                         cuttlefish::MetricsEvent::EventType event_type) {
  uint64_t now_s = now_ms / 1000;
  uint64_t now_ns = (now_ms % 1000) * 1000000;

  // "metrics_event" is the 2nd level MetricsEvent
  cuttlefish::MetricsEvent* metrics_event = cfEvent->mutable_metrics_event();
  metrics_event->set_event_type(event_type);
  metrics_event->set_os_type(metrics::osType());
  metrics_event->set_os_version(metrics::osVersion());
  metrics_event->set_vmm_type(metrics::vmmManager());
  if (metrics::vmmVersion() != "") {
    metrics_event->set_vmm_version(metrics::vmmVersion());
  }
  metrics_event->set_company(metrics::company());
  metrics_event->set_api_level(PRODUCT_SHIPPING_API_LEVEL);
  Timestamp* metrics_timestamp = metrics_event->mutable_event_time_ms();
  metrics_timestamp->set_seconds(now_s);
  metrics_timestamp->set_nanos(now_ns);
}

std::unique_ptr<LogRequest> buildLogRequest(
    uint64_t now_ms, cuttlefish::CuttlefishLogEvent* cfEvent) {
  // "log_request" is the top level LogRequest
  auto log_request = std::make_unique<LogRequest>();
  log_request->set_request_time_ms(now_ms);
  log_request->set_log_source(kLogSourceId);
  log_request->set_log_source_name(kLogSourceStr);
  ClientInfo* client_info = log_request->mutable_client_info();
  client_info->set_client_type(kCppClientType);

  // "cfLogStr" is CuttlefishLogEvent serialized
  std::string cfLogStr;
  if (!cfEvent->SerializeToString(&cfLogStr)) {
    LOG(ERROR) << "SerializeToString failed for event";
    return nullptr;
  }
  LogEvent* logEvent = log_request->add_log_event();
  logEvent->set_event_time_ms(now_ms);
  logEvent->set_source_extension(cfLogStr);
  return log_request;
}

int Clearcut::SendEvent(cuttlefish::CuttlefishLogEvent::DeviceType device_type,
                        cuttlefish::MetricsEvent::EventType event_type) {
  uint64_t now_ms = metrics::epochTimeMs();

  auto cfEvent = buildCFLogEvent(now_ms, device_type);
  buildCFMetricsEvent(now_ms, cfEvent.get(), event_type);
  auto logRequest = buildLogRequest(now_ms, cfEvent.get());
  if (!logRequest) {
    LOG(ERROR) << "failed to build LogRequest";
    return kMetricsError;
  }

  std::string logRequestStr;
  if (!logRequest->SerializeToString(&logRequestStr)) {
    LOG(ERROR) << "SerializeToString failed for log_request";
    return kMetricsError;
  }
  return metrics::postReq(logRequestStr, metrics::kProd);
}

int Clearcut::SendVMStart(cuttlefish::CuttlefishLogEvent::DeviceType device) {
  return SendEvent(
      device, cuttlefish::MetricsEvent::CUTTLEFISH_EVENT_TYPE_VM_INSTANTIATION);
}

int Clearcut::SendVMStop(cuttlefish::CuttlefishLogEvent::DeviceType device) {
  return SendEvent(device,
                   cuttlefish::MetricsEvent::CUTTLEFISH_EVENT_TYPE_VM_STOP);
}

int Clearcut::SendDeviceBoot(
    cuttlefish::CuttlefishLogEvent::DeviceType device) {
  return SendEvent(device,
                   cuttlefish::MetricsEvent::CUTTLEFISH_EVENT_TYPE_DEVICE_BOOT);
}

int Clearcut::SendLockScreen(
    cuttlefish::CuttlefishLogEvent::DeviceType device) {
  return SendEvent(
      device,
      cuttlefish::MetricsEvent::CUTTLEFISH_EVENT_TYPE_LOCK_SCREEN_AVAILABLE);
}

}  // namespace cuttlefish
