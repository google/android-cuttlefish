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
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "host/vadb/usbip/server.h"
#include "host/vadb/vhci_instrument.h"
#include "host/vadb/virtual_adb.h"

DEFINE_string(socket, "", "Socket to use to talk to USBForwarder.");

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  vadb::VirtualADB adb(FLAGS_socket);
  CHECK(adb.Init());

  vadb::VHCIInstrument vhci;
  CHECK(vhci.Init());

  vadb::usbip::Server s(adb.Pool());
  s.SetClientsAttachedByDefault(true);
  CHECK(s.Init()) << "Could not start server";
  s.Serve();
}
