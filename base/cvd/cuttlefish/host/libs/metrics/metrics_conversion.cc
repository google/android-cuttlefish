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

#include "cuttlefish/host/libs/metrics/metrics_conversion.h"

#include <chrono>

#include <fmt/format.h>
#include "google/protobuf/timestamp.pb.h"

#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/host/libs/metrics/event_type.h"
#include "external_proto/cf_guest.pb.h"
#include "external_proto/cf_host.pb.h"
#include "external_proto/cf_log.pb.h"
#include "external_proto/cf_metrics_event_v2.pb.h"
#include "external_proto/clientanalytics.pb.h"
#include "external_proto/log_source_enum.pb.h"

namespace cuttlefish {
namespace {

using google::protobuf::Timestamp;
using logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishGuest;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishGuest_EventType;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishHost;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishHost_OsType;
using logs::proto::wireless::android::cuttlefish::events::MetricsEventV2;
using wireless_android_play_playlog::ClientInfo;
using wireless_android_play_playlog::LogEvent;
using wireless_android_play_playlog::LogRequest;
using wireless_android_play_playlog::LogSourceEnum::LogSource;

static constexpr LogSource kLogSourceId = LogSource::CUTTLEFISH_METRICS;
static constexpr char kLogSourceStr[] = "CUTTLEFISH_METRICS";
static constexpr ClientInfo::ClientType kCppClientType = ClientInfo::CPLUSPLUS;

CuttlefishGuest_EventType ConvertEventType(EventType event_type) {
  switch (event_type) {
    case EventType::DeviceInstantiation:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_VM_INSTANTIATION;
    case EventType::DeviceBootStart:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_UNSPECIFIED;
    case EventType::DeviceBootComplete:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_DEVICE_BOOT_COMPLETED;
    case EventType::DeviceStop:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_VM_STOP;
  }
}

CuttlefishHost_OsType ConvertHostOs(const HostInfo& host_info) {
  switch (host_info.os) {
    case Os::Unknown:
      return CuttlefishHost_OsType::
          CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_UNSPECIFIED;
    case Os::Linux:
      switch (host_info.arch) {
        case Arch::Arm:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_AARCH32;
        case Arch::Arm64:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_AARCH64;
        case Arch::RiscV64:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_RISCV64;
        case Arch::X86:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_X86;
        case Arch::X86_64:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_X86_64;
      }
  }
}

CuttlefishLogEvent BuildCuttlefishLogEvent(const MetricsData& metrics_data) {
  CuttlefishLogEvent cf_log_event;
  cf_log_event.set_device_type(CuttlefishLogEvent::CUTTLEFISH_DEVICE_TYPE_HOST);
  cf_log_event.set_session_id(metrics_data.session_id);
  cf_log_event.set_cuttlefish_version(metrics_data.cf_common_version);
  Timestamp& timestamp = *cf_log_event.mutable_timestamp_ms();
  timestamp.set_nanos((metrics_data.now.count() % 1000) * 1000000);
  timestamp.set_seconds(metrics_data.now.count() / 1000);

  MetricsEventV2& metrics_event = *cf_log_event.mutable_metrics_event_v2();

  for (const GuestInfo& guest_info : metrics_data.guest_metrics) {
    CuttlefishGuest& guest = *metrics_event.add_guest();
    guest.set_event_type(ConvertEventType(metrics_data.event_type));
    guest.set_guest_id(fmt::format("{}-{}", metrics_data.session_id,
                                   guest_info.instance_number));
    guest.set_guest_os_version(guest_info.os_version);
  }

  CuttlefishHost& host = *metrics_event.mutable_host();
  host.set_host_os(ConvertHostOs(metrics_data.host_metrics));
  host.set_host_os_version(metrics_data.host_metrics.release);

  return cf_log_event;
}

LogRequest BuildLogRequest(std::chrono::milliseconds now,
                           const CuttlefishLogEvent& cf_log_event) {
  LogRequest log_request;
  log_request.set_request_time_ms(now.count());
  log_request.set_log_source(kLogSourceId);
  log_request.set_log_source_name(kLogSourceStr);

  ClientInfo& client_info = *log_request.mutable_client_info();
  client_info.set_client_type(kCppClientType);

  LogEvent& log_event = *log_request.add_log_event();
  log_event.set_event_time_ms(now.count());
  log_event.set_source_extension(cf_log_event.SerializeAsString());
  return log_request;
}

}  // namespace

LogRequest ConstructLogRequest(const MetricsData& metrics_data) {
  CuttlefishLogEvent cf_log_event = BuildCuttlefishLogEvent(metrics_data);
  return BuildLogRequest(metrics_data.now, cf_log_event);
}

}  // namespace cuttlefish
