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

#include <glog/logging.h>
#include <gflags/gflags.h>

#include <unistd.h>

#include "common/libs/strings/str_split.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/adb_connection_maintainer/adb_connection_maintainer.h"

DEFINE_string(ports, "", "Comma-separated list of ports to 'adb connect' to");

namespace {
void LaunchConnectionMaintainerThread(int port) {
  std::thread(cvd::EstablishAndMaintainConnection, port).detach();
}

std::vector<int> ParsePortsList(std::string ports) {
  std::replace(ports.begin(), ports.end(), ',', ' ');
  std::istringstream port_stream{ports};
  return {std::istream_iterator<int>{port_stream},
          std::istream_iterator<int>{}};
}

[[noreturn]] void SleepForever() {
  while (true) {
    sleep(std::numeric_limits<unsigned int>::max());
  }
}
}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  CHECK(!FLAGS_ports.empty()) << "Must specify --ports flag";

  for (auto port : ParsePortsList(FLAGS_ports)) {
    LaunchConnectionMaintainerThread(port);
  }

  SleepForever();
}
