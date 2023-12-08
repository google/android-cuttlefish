/*
 * Copyright 2023 The Android Open Source Project
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

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/utils/result.h"
#include "host/libs/metrics/metrics_receiver.h"

namespace cuttlefish {
namespace {

Result<void> MetricsLauncherMain() {
  while (true) {
    std::cout << "Please choose an action: \n";
    std::cout << "  start - send start event to cuttlefish metrics client \n";
    std::cout << "  stop - send stop event to cuttlefish metrics client \n";
    std::cout << "  boot - send boot event to cuttlefish metrics client\n";
    std::cout << "  lock - send lock event to cuttlefish metrics client\n";
    std::cout << "  atest - send launch command to atest metrics client \n";
    std::cout << "  exit - exit the program \n";

    std::string command;
    std::getline(std::cin, command);

    if (command == "start") {
      cuttlefish::MetricsReceiver::LogMetricsVMStart();
    } else if (command == "stop") {
      cuttlefish::MetricsReceiver::LogMetricsVMStop();
    } else if (command == "boot") {
      cuttlefish::MetricsReceiver::LogMetricsDeviceBoot();
    } else if (command == "lock") {
      cuttlefish::MetricsReceiver::LogMetricsLockScreen();
    } else if (command == "exit") {
      break;
    } else {
      LOG(ERROR) << "Unknown command: " << command;
    }
  }
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  cuttlefish::Result<void> result = cuttlefish::MetricsLauncherMain();
  if (!result.ok()) {
    LOG(ERROR) << result.error().FormatForEnv();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
