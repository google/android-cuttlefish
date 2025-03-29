//
// Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/commands/run_cvd/launch/metrics.h"

#include <optional>

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/config/command_source.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"

namespace cuttlefish {

std::optional<MonitorCommand> MetricsService(const CuttlefishConfig& config) {
  if (config.enable_metrics() != cuttlefish::CuttlefishConfig::Answer::kYes) {
    return {};
  }
  return Command(MetricsBinary());
}

}  // namespace cuttlefish
