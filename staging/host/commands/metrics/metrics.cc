//
// Copyright (C) 2020 The Android Open Source Project
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

#include <gflags/gflags.h>

#include "common/libs/utils/tee_logging.h"
#include "host/commands/metrics/host_receiver.h"
#include "host/commands/metrics/metrics_defs.h"

using cuttlefish::MetricsExitCodes;

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  auto config = cuttlefish::CuttlefishConfig::Get();
  CHECK(config) << "Could not open cuttlefish config";
  auto instance = config->ForDefaultInstance();
  auto metrics_log_path = instance.PerInstanceLogPath("metrics.log");
  if (instance.run_as_daemon()) {
    android::base::SetLogger(
        cuttlefish::LogToFiles({metrics_log_path, instance.launcher_log_path()}));
  } else {
    android::base::SetLogger(
        cuttlefish::LogToStderrAndFiles(
            {metrics_log_path, instance.launcher_log_path()}));
  }
  if (config->enable_metrics() != cuttlefish::CuttlefishConfig::kYes) {
    LOG(ERROR) << "metrics not enabled, but metrics were launched.";
    return MetricsExitCodes::kInvalidHostConfiguration;
  }

  cuttlefish::MetricsHostReceiver host_receiver(*config);
  if (!host_receiver.Initialize()) {
    LOG(ERROR) << "metrics host_receiver failed to init";
    return MetricsExitCodes::kMetricsError;
  }
  LOG(INFO) << "Metrics started";
  host_receiver.Join();
  return MetricsExitCodes::kMetricsError;
}
