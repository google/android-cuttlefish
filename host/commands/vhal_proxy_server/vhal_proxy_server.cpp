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

#include <VehicleUtils.h>
#include <aidl/android/hardware/automotive/vehicle/VehicleApPowerStateConfigFlag.h>
#include <android-base/logging.h>
#include <cutils/properties.h>

#include <memory>

namespace {

using ::aidl::android::hardware::automotive::vehicle::
    VehicleApPowerStateConfigFlag;
using ::android::hardware::automotive::utils::VsockConnectionInfo;
using ::android::hardware::automotive::vehicle::toInt;
using ::android::hardware::automotive::vehicle::fake::FakeVehicleHardware;
using ::android::hardware::automotive::vehicle::virtualization::
    GrpcVehicleProxyServer;

}  // namespace

// A GRPC server for VHAL running on the guest Android.
// argv[1]: Config directory path containing property config file (e.g.
// DefaultProperties.json).
// argv[2]: The IP address for this server.
// argv[3]: The vsock address for this server.
int main(int argc, char* argv[]) {
  CHECK(argc >= 4) << "Not enough arguments, require at least 3: config file "
                      "path, IP address, vsock address";

  std::string eth_addr = argv[2];
  std::string grpc_server_addr = argv[3];
  std::vector<std::string> listen_addrs = {grpc_server_addr, eth_addr};

  // For cuttlefish we support S2R and S2D.
  int32_t s2rS2dConfig =
      toInt(VehicleApPowerStateConfigFlag::ENABLE_DEEP_SLEEP_FLAG) |
      toInt(VehicleApPowerStateConfigFlag::ENABLE_HIBERNATION_FLAG);
  auto fake_hardware =
      std::make_unique<FakeVehicleHardware>(argv[1], "", false, s2rS2dConfig);
  auto proxy_server = std::make_unique<GrpcVehicleProxyServer>(
      listen_addrs, std::move(fake_hardware));

  LOG(INFO) << "VHAL Server is listening on " << grpc_server_addr << ", "
            << eth_addr;

  proxy_server->Start().Wait();
  return 0;
}
