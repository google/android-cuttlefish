/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <algorithm>
#include <iterator>
#include <limits>
#include <sstream>
#include <thread>
#include <vector>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include <unistd.h>
#include <host/commands/kernel_log_monitor/kernel_log_server.h>
#include <host/commands/kernel_log_monitor/utils.h>

#include "common/libs/fs/shared_fd.h"
#include "host/frontend/adb_connector/adb_connection_maintainer.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"

DEFINE_string(addresses, "", "Comma-separated list of addresses to "
                             "'adb connect' to");

namespace cuttlefish {
namespace {
void LaunchConnectionMaintainerThread(const std::string& address) {
  std::thread(EstablishAndMaintainConnection, address).detach();
}

std::vector<std::string> ParseAddressList(std::string ports) {
  std::replace(ports.begin(), ports.end(), ',', ' ');
  std::istringstream port_stream{ports};
  return {std::istream_iterator<std::string>{port_stream},
          std::istream_iterator<std::string>{}};
}

[[noreturn]] void SleepForever() {
  while (true) {
    sleep(std::numeric_limits<unsigned int>::max());
  }
}

}  // namespace

int AdbConnectorMain(int argc, char* argv[]) {
  DefaultSubprocessLogging(argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  CHECK(!FLAGS_addresses.empty()) << "Must specify --addresses flag";

  for (const auto& address : ParseAddressList(FLAGS_addresses)) {
    LaunchConnectionMaintainerThread(address);
  }

  SleepForever();
}

}  // namespace cuttlefish

int main(int argc, char* argv[]) {
  return cuttlefish::AdbConnectorMain(argc, argv);
}
