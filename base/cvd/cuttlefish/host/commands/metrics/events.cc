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

#include "cuttlefish/host/commands/metrics/events.h"

#include <sys/utsname.h>

#include "google/protobuf/timestamp.pb.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/metrics/utils.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"
#include "cuttlefish/host/libs/metrics/metrics_defs.h"
#include "external_proto/cf_log.pb.h"
#include "external_proto/cf_metrics_event.pb.h"
#include "external_proto/clientanalytics.pb.h"
#include "external_proto/log_source_enum.pb.h"

namespace cuttlefish::metrics {

namespace {

using google::protobuf::Timestamp;
using logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent;
using logs::proto::wireless::android::cuttlefish::events::MetricsEvent;
using wireless_android_play_playlog::ClientInfo;
using wireless_android_play_playlog::LogEvent;
using wireless_android_play_playlog::LogRequest;
using wireless_android_play_playlog::LogSourceEnum::LogSource;

// TODO: 403646742 - this value previously came from the build, need to revisit
static constexpr int PRODUCT_SHIPPING_API_LEVEL = 37;

static constexpr LogSource kLogSourceId = LogSource::CUTTLEFISH_METRICS;

static constexpr char kLogSourceStr[] = "CUTTLEFISH_METRICS";
static constexpr ClientInfo::ClientType kCppClientType = ClientInfo::CPLUSPLUS;

std::pair<uint64_t, uint64_t> ConvertMillisToTime(uint64_t millis) {
  uint64_t seconds = millis / 1000;
  uint64_t nanos = (millis % 1000) * 1000000;
  return {seconds, nanos};
}

std::unique_ptr<CuttlefishLogEvent> BuildCfLogEvent(uint64_t now_ms) {
  auto [now_s, now_ns] = ConvertMillisToTime(now_ms);

  // "cfEvent" is the top level CuttlefishLogEvent
  auto cfEvent = std::make_unique<CuttlefishLogEvent>();
  cfEvent->set_device_type(CuttlefishLogEvent::CUTTLEFISH_DEVICE_TYPE_HOST);
  cfEvent->set_session_id(GenerateSessionId(now_ms));

  if (!GetCfVersion().empty()) {
    cfEvent->set_cuttlefish_version(GetCfVersion());
  }

  Timestamp* timestamp = cfEvent->mutable_timestamp_ms();
  timestamp->set_seconds(now_s);
  timestamp->set_nanos(now_ns);

  return cfEvent;
}

MetricsEvent::OsType GetOsType() {
  struct utsname buf;
  if (uname(&buf) != 0) {
    LOG(ERROR) << "failed to retrieve system information";
    return MetricsEvent::CUTTLEFISH_OS_TYPE_UNSPECIFIED;
  }
  std::string sysname(buf.sysname);
  std::string machine(buf.machine);

  if (sysname != "Linux") {
    return MetricsEvent::CUTTLEFISH_OS_TYPE_UNSPECIFIED;
  }
  if (machine == "x86_64") {
    return MetricsEvent::CUTTLEFISH_OS_TYPE_LINUX_X86_64;
  }
  if (machine == "x86") {
    return MetricsEvent::CUTTLEFISH_OS_TYPE_LINUX_X86;
  }
  if (machine == "aarch64" || machine == "arm64") {
    return MetricsEvent::CUTTLEFISH_OS_TYPE_LINUX_AARCH64;
  }
  if (machine[0] == 'a') {
    return MetricsEvent::CUTTLEFISH_OS_TYPE_LINUX_AARCH32;
  }
  return MetricsEvent::CUTTLEFISH_OS_TYPE_UNSPECIFIED;
}

MetricsEvent::VmmType GetVmmManager() {
  auto config = CuttlefishConfig::Get();
  CHECK(config) << "Could not open cuttlefish config";
  auto vmm = config->vm_manager();
  if (vmm == VmmMode::kCrosvm) {
    return MetricsEvent::CUTTLEFISH_VMM_TYPE_CROSVM;
  }
  if (vmm == VmmMode::kQemu) {
    return MetricsEvent::CUTTLEFISH_VMM_TYPE_QEMU;
  }
  return MetricsEvent::CUTTLEFISH_VMM_TYPE_UNSPECIFIED;
}

// Builds the 2nd level MetricsEvent.
void AddCfMetricsEventToLog(uint64_t now_ms, CuttlefishLogEvent* cfEvent,
                            MetricsEvent::EventType event_type) {
  auto [now_s, now_ns] = ConvertMillisToTime(now_ms);

  // "metrics_event" is the 2nd level MetricsEvent
  MetricsEvent* metrics_event = cfEvent->mutable_metrics_event();
  metrics_event->set_event_type(event_type);
  metrics_event->set_os_type(GetOsType());
  metrics_event->set_os_version(GetOsVersion());
  metrics_event->set_vmm_type(GetVmmManager());

  if (!GetVmmVersion().empty()) {
    metrics_event->set_vmm_version(GetVmmVersion());
  }

  metrics_event->set_company(GetCompany());
  metrics_event->set_api_level(PRODUCT_SHIPPING_API_LEVEL);

  Timestamp* metrics_timestamp = metrics_event->mutable_event_time();
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

int SendEvent(MetricsEvent::EventType event_type) {
  uint64_t now_ms = GetEpochTimeMs();

  auto cfEvent = BuildCfLogEvent(now_ms);
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

  return PostRequest(logRequestStr, ClearcutServer::kProd);
}

}  // namespace

int SendVMStart() {
  return SendEvent(MetricsEvent::CUTTLEFISH_EVENT_TYPE_VM_INSTANTIATION);
}

int SendVMStop() {
  return SendEvent(MetricsEvent::CUTTLEFISH_EVENT_TYPE_VM_STOP);
}

int SendDeviceBoot() {
  return SendEvent(MetricsEvent::CUTTLEFISH_EVENT_TYPE_DEVICE_BOOT);
}

int SendLockScreen() {
  return SendEvent(MetricsEvent::CUTTLEFISH_EVENT_TYPE_LOCK_SCREEN_AVAILABLE);
}

}  // namespace cuttlefish::metrics
