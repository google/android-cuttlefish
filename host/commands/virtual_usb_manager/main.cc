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

#include <string>
#include <thread>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/usbip/server.h"
#include "host/libs/vadb/virtual_adb_server.h"

DEFINE_int32(
    usb_v1_fd, -1,
    "A file descriptor pointing to the USB v1 open socket or -1 to create it");

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = vsoc::CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Unable to get config object";
    return 1;
  }

  cvd::SharedFD usb_v1_server;

  if (FLAGS_usb_v1_fd < 0) {
    auto socket_name = config->usb_v1_socket_name();
    LOG(INFO) << "Starting server at " << socket_name;
    usb_v1_server = cvd::SharedFD::SocketLocalServer(socket_name.c_str(), false,
                                                     SOCK_STREAM, 0666);
  } else {
    usb_v1_server = cvd::SharedFD::Dup(FLAGS_usb_v1_fd);
  }

  if (!usb_v1_server->IsOpen()) {
    LOG(ERROR) << "Error openning USB v1 server: " << usb_v1_server->StrError();
    return 2;
  }

  vadb::VirtualADBServer adb_{usb_v1_server, config->vhci_port(),
                              config->usb_ip_socket_name()};
  vadb::usbip::Server usbip_{config->usb_ip_socket_name(), adb_.Pool()};

  CHECK(usbip_.Init()) << "Could not start USB/IP server";

  for (;;) {
    cvd::SharedFDSet fd_read;
    fd_read.Zero();

    adb_.BeforeSelect(&fd_read);
    usbip_.BeforeSelect(&fd_read);

    int ret = cvd::Select(&fd_read, nullptr, nullptr, nullptr);
    if (ret <= 0) continue;

    adb_.AfterSelect(fd_read);
    usbip_.AfterSelect(fd_read);
  }

  return 0;
}
