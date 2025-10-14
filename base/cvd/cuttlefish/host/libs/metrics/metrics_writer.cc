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

#include "cuttlefish/host/libs/metrics/metrics_writer.h"

#include <chrono>
#include <string>

#include <fmt/format.h>
#include <google/protobuf/text_format.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/random.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/metrics/event_type.h"

namespace cuttlefish {
namespace {

std::string GenerateFilenameSuffix() {
  const std::string nums("0123456789");
  return GenerateRandomString(nums, 10);
}

}  // namespace

Result<void> WriteMetricsEvent(
    EventType event_type, const std::string& metrics_directory,
    const wireless_android_play_playlog::LogRequest& log_request) {
  const std::string event_filepath = fmt::format(
      "{}/{}_{}_{}.txtpb", metrics_directory, EventTypeString(event_type),
      std::chrono::system_clock::now(), GenerateFilenameSuffix());
  std::string text_proto_out;
  google::protobuf::TextFormat::PrintToString(log_request, &text_proto_out);
  CF_EXPECT(WriteNewFile(event_filepath, text_proto_out));
  return {};
}

}  // namespace cuttlefish
