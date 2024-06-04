/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "FakeVehicleHardware.h"
#include "GRPCVehicleProxyServer.h"
#include "vsockinfo.h"

#include <android-base/logging.h>
#include <cutils/properties.h>
#include <linux/vm_sockets.h>
#include <sys/socket.h>

#include <memory>

using ::android::hardware::automotive::utils::VsockConnectionInfo;
using ::android::hardware::automotive::vehicle::fake::FakeVehicleHardware;
using ::android::hardware::automotive::vehicle::virtualization::
    GrpcVehicleProxyServer;

// A GRPC server for VHAL running on the guest Android.
// argv[1]: Config directory path containing property config file (e.g.
// DefaultProperties.json).
// argv[2]: The vsock port number used by this server.
int main(int argc, char* argv[]) {
  CHECK(argc >= 3) << "Not enough arguments, require at least 2: config file "
                      "path and vsock port";

  unsigned int port;
  CHECK(android::base::ParseUint(argv[2], &port))
      << "Failed to parse port as uint";
  VsockConnectionInfo vsock = {.cid = VMADDR_CID_HOST, .port = port};

  auto eth_addr = fmt::format("localhost:{}", port);
  std::vector<std::string> listen_addrs = {vsock.str(), eth_addr};

  auto fake_hardware =
      std::make_unique<FakeVehicleHardware>(argv[1], "", false);
  auto proxy_server = std::make_unique<GrpcVehicleProxyServer>(
      listen_addrs, std::move(fake_hardware));

  LOG(INFO) << "VHAL Server is listening on " << vsock.str() << ", "
            << eth_addr;

  proxy_server->Start().Wait();
  return 0;
}
