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

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/metrics/metrics_environment.h"
#include "external_proto/cf_log.pb.h"

namespace cuttlefish {
namespace {

using logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent;

Command BuildCommand(const std::string& serialized_proto) {
  Command command(MetricsTransmitterBinary());
  command.AddParameter("--environment");
  command.AddParameter(kProduction);
  command.AddParameter("--serialized_proto");
  command.AddParameter(serialized_proto);
  return command;
}

}  // namespace

Result<void> TransmitMetrics(
    const logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent&
        cf_log_event) {
  Command transmission_command = BuildCommand(cf_log_event.SerializeAsString());
  CF_EXPECT(RunAndCaptureStdout(std::move(transmission_command)),
            "Failed to transmit metrics.");
  return {};
}

}  // namespace cuttlefish
