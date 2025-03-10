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

#include "host/commands/metrics/events.h"
#include "common/libs/utils/flag_parser.h"
#include "host/commands/metrics/metrics_defs.h"
#include "host/commands/metrics/proto/cf_metrics_proto.h"
#include "host/commands/metrics/utils.h"
#include "shared/api_level.h"

namespace cuttlefish {

namespace {

constexpr int kLogSourceId = 1753;
constexpr char kLogSourceStr[] = "CUTTLEFISH_METRICS";
constexpr int kCppClientType =
    19;  // C++ native client type (clientanalytics.proto)

std::pair<uint64_t, uint64_t> ConvertMillisToTime(uint64_t millis) {
  uint64_t seconds = millis / 1000;
  uint64_t nanos = (millis % 1000) * 1000000;
  return {seconds, nanos};
}

std::unique_ptr<CuttlefishLogEvent> BuildCfLogEvent(
    uint64_t now_ms, CuttlefishLogEvent::DeviceType device_type) {
  auto [now_s, now_ns] = ConvertMillisToTime(now_ms);

  // "cfEvent" is the top level CuttlefishLogEvent
  auto cfEvent = std::make_unique<CuttlefishLogEvent>();
  cfEvent->set_device_type(device_type);
  cfEvent->set_session_id(metrics::GenerateSessionId(now_ms));

  if (!metrics::GetCfVersion().empty()) {
    cfEvent->set_cuttlefish_version(metrics::GetCfVersion());
  }

  Timestamp* timestamp = cfEvent->mutable_timestamp_ms();
  timestamp->set_seconds(now_s);
  timestamp->set_nanos(now_ns);

  return cfEvent;
}

// Builds the 2nd level MetricsEvent.
void AddCfMetricsEventToLog(uint64_t now_ms, CuttlefishLogEvent* cfEvent,
                            MetricsEvent::EventType event_type) {
  auto [now_s, now_ns] = ConvertMillisToTime(now_ms);

  // "metrics_event" is the 2nd level MetricsEvent
  cuttlefish::MetricsEvent* metrics_event = cfEvent->mutable_metrics_event();
  metrics_event->set_event_type(event_type);
  metrics_event->set_os_type(metrics::GetOsType());
  metrics_event->set_os_version(metrics::GetOsVersion());
  metrics_event->set_vmm_type(metrics::GetVmmManager());

  if (!metrics::GetVmmVersion().empty()) {
    metrics_event->set_vmm_version(metrics::GetVmmVersion());
  }

  metrics_event->set_company(metrics::GetCompany());
  metrics_event->set_api_level(PRODUCT_SHIPPING_API_LEVEL);

  Timestamp* metrics_timestamp = metrics_event->mutable_event_time_ms();
  metrics_timestamp->set_seconds(now_s);
  metrics_timestamp->set_nanos(now_ns);
}

std::unique_ptr<LogRequest> BuildLogRequest(uint64_t now_ms,
                                            CuttlefishLogEvent* cfEvent) {
  // "log_request" is the top level LogRequest
  auto log_request = std::make_unique<LogRequest>();
  log_request->set_request_time_ms(now_ms);
  log_request->set_log_source(kLogSourceId);
  log_request->set_log_source_name(kLogSourceStr);

  ClientInfo* client_info = log_request->mutable_client_info();
  client_info->set_client_type(kCppClientType);

  std::string cfLogStr;
  if (!cfEvent->SerializeToString(&cfLogStr)) {
    LOG(ERROR) << "Serialization failed for event";
    return nullptr;
  }

  LogEvent* logEvent = log_request->add_log_event();
  logEvent->set_event_time_ms(now_ms);
  logEvent->set_source_extension(cfLogStr);

  return log_request;
}

}  // namespace

Clearcut::Clearcut() = default;
Clearcut::~Clearcut() = default;

int Clearcut::SendEvent(CuttlefishLogEvent::DeviceType device_type,
                        MetricsEvent::EventType event_type) {
  uint64_t now_ms = metrics::GetEpochTimeMs();

  auto cfEvent = BuildCfLogEvent(now_ms, device_type);
  AddCfMetricsEventToLog(now_ms, cfEvent.get(), event_type);

  auto logRequest = BuildLogRequest(now_ms, cfEvent.get());
  if (!logRequest) {
    LOG(ERROR) << "Failed to build LogRequest";
    return MetricsExitCodes::kMetricsError;
  }

  std::string logRequestStr;
  if (!logRequest->SerializeToString(&logRequestStr)) {
    LOG(ERROR) << "Serialization failed for LogRequest";
    return MetricsExitCodes::kMetricsError;
  }

  return metrics::PostRequest(logRequestStr, metrics::kProd);
}

int Clearcut::SendVMStart(CuttlefishLogEvent::DeviceType device) {
  return SendEvent(device,
                   MetricsEvent::CUTTLEFISH_EVENT_TYPE_VM_INSTANTIATION);
}

int Clearcut::SendVMStop(CuttlefishLogEvent::DeviceType device) {
  return SendEvent(device, MetricsEvent::CUTTLEFISH_EVENT_TYPE_VM_STOP);
}

int Clearcut::SendDeviceBoot(CuttlefishLogEvent::DeviceType device) {
  return SendEvent(device, MetricsEvent::CUTTLEFISH_EVENT_TYPE_DEVICE_BOOT);
}

int Clearcut::SendLockScreen(CuttlefishLogEvent::DeviceType device) {
  return SendEvent(device,
                   MetricsEvent::CUTTLEFISH_EVENT_TYPE_LOCK_SCREEN_AVAILABLE);
}

}  // namespace cuttlefish