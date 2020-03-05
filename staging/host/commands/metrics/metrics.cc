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

#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/fs/tee.h"
#include "host/commands/metrics/metrics_defs.h"
#include "host/libs/config/cuttlefish_config.h"

using cvd::MetricsExitCodes;

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, false);

  cvd::TeeStderrToFile stderr_tee;
  auto config = vsoc::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  auto metrics_log_path = instance.PerInstancePath("metrics.log");
  stderr_tee.SetFile(cvd::SharedFD::Creat(metrics_log_path.c_str(), 0755));

  if (config->enable_metrics() != vsoc::CuttlefishConfig::kYes) {
    LOG(ERROR) << "metrics not enabled, but metrics were launched.";
    return cvd::MetricsExitCodes::kInvalidHostConfiguration;
  }

  while (true) {
    // do nothing
    sleep(std::numeric_limits<unsigned int>::max());
  }
  return cvd::MetricsExitCodes::kMetricsError;
}
