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

#include "cuttlefish/host/commands/metrics/clearcut_protos.h"

#include <vector>

#include "external_proto/cf_log.pb.h"
#include "external_proto/clientanalytics.pb.h"
#include "external_proto/log_source_enum.pb.h"

namespace cuttlefish::metrics {

namespace {

using logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent;
using wireless_android_play_playlog::ClientInfo;
using wireless_android_play_playlog::LogEvent;
using wireless_android_play_playlog::LogRequest;
using wireless_android_play_playlog::LogSourceEnum::LogSource;

static constexpr LogSource kLogSourceId = LogSource::CUTTLEFISH_METRICS;

static constexpr char kLogSourceStr[] = "CUTTLEFISH_METRICS";
static constexpr ClientInfo::ClientType kCppClientType = ClientInfo::CPLUSPLUS;

}  // namespace

LogEvent BuildLogEvent(uint64_t now_ms, const CuttlefishLogEvent& cf_event) {
  LogEvent log_event;
  log_event.set_event_time_ms(now_ms);
  log_event.set_source_extension(cf_event.SerializeAsString());
  return log_event;
}

LogRequest BuildLogRequest(uint64_t now_ms, LogEvent event) {
  std::vector<LogEvent> events;
  events.emplace_back(std::move(event));
  return BuildLogRequest(now_ms, events);
}

LogRequest BuildLogRequest(uint64_t now_ms, std::vector<LogEvent> events) {
  LogRequest log_request;
  log_request.set_request_time_ms(now_ms);
  log_request.set_log_source(kLogSourceId);
  log_request.set_log_source_name(kLogSourceStr);

  ClientInfo& client_info = *log_request.mutable_client_info();
  client_info.set_client_type(kCppClientType);

  for (LogEvent event : events) {
    *log_request.add_log_event() = std::move(event);
  }

  return log_request;
}

}  // namespace cuttlefish::metrics
