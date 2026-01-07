/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/metrics/debug_reader.h"

#include <string>

#include <google/protobuf/text_format.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/result/result.h"
#include "external_proto/cf_log.pb.h"

namespace cuttlefish {
namespace {

using logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent;

}  // namespace

Result<std::string> GetSerializedEventProto(const std::string& event_filepath) {
  const std::string proto_string = CF_EXPECT(ReadFileContents(event_filepath));
  CuttlefishLogEvent cf_log_event;
  CF_EXPECTF(google::protobuf::TextFormat::ParseFromString(proto_string,
                                                           &cf_log_event),
             "Unable to parse proto from file contents at: {}", event_filepath);
  return cf_log_event.SerializeAsString();
}

}  // namespace cuttlefish
