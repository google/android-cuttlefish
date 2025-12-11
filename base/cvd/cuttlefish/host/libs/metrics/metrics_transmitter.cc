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

#include <string>

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/libs/metrics/metrics_environment.h"
#include "cuttlefish/result/result.h"
#include "external_proto/cf_log.pb.h"

namespace cuttlefish {
namespace {

using logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent;

// the serialized string is what is transmitted
// the binary argument is base64-encoded to avoid string mangling
Result<std::string> GetSerializedProtoArgument(
    const CuttlefishLogEvent& cf_log_event) {
  const std::string serialized_event = cf_log_event.SerializeAsString();
  std::string encoded_str_out;
  CF_EXPECTF(EncodeBase64(serialized_event.c_str(), serialized_event.size(),
                          &encoded_str_out),
             "Unable to base64-encode string: {}", serialized_event);
  return encoded_str_out;
}

Command BuildCommand(const std::string& transmitter_binary,
                     const std::string& serialized_proto) {
  return Command(transmitter_binary)
      .AddParameter("--environment")
      .AddParameter(kClearcutProduction)
      .AddParameter("--serialized_proto")
      .AddParameter(serialized_proto);
}

}  // namespace

Result<void> TransmitMetrics(const std::string& transmitter_binary,
                             const CuttlefishLogEvent& cf_log_event) {
  Command transmission_command = BuildCommand(
      transmitter_binary, CF_EXPECT(GetSerializedProtoArgument(cf_log_event)));
  CF_EXPECT(RunAndCaptureStdout(std::move(transmission_command)),
            "Failed to transmit metrics.");
  return {};
}

}  // namespace cuttlefish
