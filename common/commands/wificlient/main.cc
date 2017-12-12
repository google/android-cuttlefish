/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/wifi/netlink.h"
#include "common/libs/wifi/virtual_wifi.h"

DEFINE_string(router, "cvd-wifirouter", "Path to WIFI Router Unix socket.");
DEFINE_string(iface, "cf-wlan0", "Name of the WLAN interface to create.");
DEFINE_string(macaddr, "00:43:56:44:80:01", "MAC address for new interface");
DEFINE_string(wifirouter_socket, "cvd-wifirouter",
              "Name of the wifirouter unix domain socket.");

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::unique_ptr<cvd::Netlink> nl(new cvd::Netlink(FLAGS_wifirouter_socket));
  LOG_IF(FATAL, !nl->Init());

  std::unique_ptr<cvd::VirtualWIFI> radio(
      new cvd::VirtualWIFI(nl.get(), FLAGS_iface, FLAGS_macaddr));
  LOG_IF(FATAL, !radio->Init());

  pause();
}
