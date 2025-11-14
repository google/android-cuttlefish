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

#include <cstdlib>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/metrics/metrics_conversion.h"
#include "cuttlefish/host/commands/metrics/metrics_flags.h"
#include "cuttlefish/host/commands/metrics/metrics_transmission.h"
#include "cuttlefish/result/result.h"
#include "external_proto/clientanalytics.pb.h"

namespace cuttlefish {
namespace {

Result<void> MetricsMain(int argc, char** argv) {
  const MetricsFlags flags =
      CF_EXPECT(ProcessFlags(argc, argv),
                "Transmitter could not process command line flags.");
  const wireless_android_play_playlog::LogRequest log_request =
      BuildLogRequest(flags.serialized_proto);
  CF_EXPECT(TransmitMetricsEvent(log_request, flags.environment),
            "Transmission of metrics failed.");
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  cuttlefish::LogToStderr();
  cuttlefish::Result<void> result = cuttlefish::MetricsMain(argc, argv);
  if (result.ok()) {
    return EXIT_SUCCESS;
  } else {
    LOG(ERROR) << result.error();
    return EXIT_FAILURE;
  }
}
