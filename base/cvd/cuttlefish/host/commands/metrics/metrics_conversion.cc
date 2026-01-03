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

#include "cuttlefish/host/commands/metrics/metrics_conversion.h"

#include <chrono>
#include <string>

#include "external_proto/clientanalytics.pb.h"
#include "external_proto/log_source_enum.pb.h"

namespace cuttlefish {
namespace {

using wireless_android_play_playlog::ClientInfo;
using wireless_android_play_playlog::LogEvent;
using wireless_android_play_playlog::LogRequest;
using wireless_android_play_playlog::LogSourceEnum::LogSource;

constexpr LogSource kLogSourceId = LogSource::CUTTLEFISH_METRICS;
constexpr char kLogSourceStr[] = "CUTTLEFISH_METRICS";
constexpr ClientInfo::ClientType kCppClientType = ClientInfo::CPLUSPLUS;

}  // namespace

LogRequest BuildLogRequest(const std::string& serialized_cf_log_event) {
  LogRequest log_request;
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch());
  log_request.set_request_time_ms(now.count());
  log_request.set_log_source(kLogSourceId);
  log_request.set_log_source_name(kLogSourceStr);

  ClientInfo& client_info = *log_request.mutable_client_info();
  client_info.set_client_type(kCppClientType);

  LogEvent& log_event = *log_request.add_log_event();
  log_event.set_event_time_ms(now.count());
  log_event.set_source_extension(serialized_cf_log_event);
  return log_request;
}

}  // namespace cuttlefish
