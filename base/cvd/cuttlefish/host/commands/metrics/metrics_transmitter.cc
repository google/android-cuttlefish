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

#include <android-base/logging.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/metrics/metrics_conversion.h"
#include "cuttlefish/host/commands/metrics/metrics_flags.h"
#include "cuttlefish/host/commands/metrics/metrics_transmission.h"
#include "external_proto/clientanalytics.pb.h"

namespace cuttlefish {
namespace {

MetricsFlags ProcessMetricsFlags(int argc, char** argv) {
  Result<MetricsFlags> process_result = ProcessFlags(argc, argv);
  CHECK(process_result.ok()) << "Could not process command line flags: "
                             << process_result.error().FormatForEnv();
  return *process_result;
}

int MetricsMain(const MetricsFlags& flags) {
  const wireless_android_play_playlog::LogRequest log_request =
      BuildLogRequest(flags.serialized_proto);
  Result<void> transmit_result =
      TransmitMetricsEvent(log_request, flags.environment);
  if (transmit_result.ok()) {
    return EXIT_SUCCESS;
  } else {
    // TODO CJR: log something
    return EXIT_FAILURE;
  }
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  // TODO CJR: consider adding filepath flag for the new metrics/metrics.log and
  // updating logger if it exists
  const cuttlefish::MetricsFlags flags =
      cuttlefish::ProcessMetricsFlags(argc, argv);
  return cuttlefish::MetricsMain(flags);
}
