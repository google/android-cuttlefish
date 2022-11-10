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

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <curl/curl.h>
#include <gflags/gflags.h>
#include <json/json.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <chrono>
#include <ctime>
#include <iostream>

#include "common/libs/utils/tee_logging.h"
#include "host/commands/metrics/host_receiver.h"
#include "host/commands/metrics/metrics_defs.h"
#include "host/commands/metrics/proto/cf_metrics_proto.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/msg_queue/msg_queue.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

using cuttlefish::MetricsExitCodes;
using std::cout;
using std::endl;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

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
    return cuttlefish::MetricsExitCodes::kInvalidHostConfiguration;
  }

  cuttlefish::MetricsHostReceiver host_receiver(*config);
  if (!host_receiver.Initialize()) {
    LOG(ERROR) << "metrics host_receiver failed to init";
    return cuttlefish::MetricsExitCodes::kMetricsError;
  }
  LOG(INFO) << "Metrics started";
  host_receiver.Join();
  return cuttlefish::MetricsExitCodes::kMetricsError;
}
