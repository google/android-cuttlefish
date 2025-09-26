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

#include "cuttlefish/host/libs/metrics/metrics_orchestrator.h"

#include <string>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/metrics/host_metrics.h"
#include "cuttlefish/host/libs/metrics/metrics_writer.h"

namespace cuttlefish {

Result<void> RunVmInstantiationMetrics(const std::string& metrics_directory) {
  HostMetrics host_metrics =
      CF_EXPECT(GetHostMetrics(), "Failed to gather host metrics data.");
  // TODO: chadreynolds - gather the rest of the data (guest/flag information)
  // TODO: chadreynolds - convert data to the proto representation
  CF_EXPECT(WriteMetricsEvent(metrics_directory, host_metrics),
            "Failed to write host metrics to file.");
  // TODO: chadreynolds - if <TBD> condition, transmit metrics event as well
  return {};
}

}  // namespace cuttlefish
