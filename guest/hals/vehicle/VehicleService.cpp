/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "DefaultVehicleHal.h"
#include "GRPCVehicleHardware.h"
#include "vsockinfo.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <cutils/properties.h>
#include <linux/vm_sockets.h>
#include <sys/socket.h>

#include <chrono>
#include <memory>
#include <utility>

using ::android::hardware::automotive::utils::VsockConnectionInfo;
using ::android::hardware::automotive::vehicle::DefaultVehicleHal;
using ::android::hardware::automotive::vehicle::virtualization::
    GRPCVehicleHardware;

const char* kServiceName =
    "android.hardware.automotive.vehicle.IVehicle/default";
const char* kBootConfigPort = "ro.boot.vhal_proxy_server_port";
const char* kAutoEthNamespaceSetupProp =
    "android.car.auto_eth_namespace_setup_complete";
const char* kVsockServiceName = "vendor.vehicle-cf-vsock";
const char* kEthServerAddr = "192.168.98.1";

int main(int argc, char* argv[]) {
  bool useVsock = false;

  if (argc > 1 && strcmp(argv[1], "vsock") == 0) {
    if (property_get_bool(kAutoEthNamespaceSetupProp, false)) {
      LOG(INFO) << "Skip starting VHAL in vsock mode since ethernet is enabled";
      return 0;
    }

    // If we are not exiting intentionally, we need to turn off oneshot so that
    // VHAL will be restarted in case it exits. vendor.vehicle-cf-eth does not
    // have oneshot in the rc file so nothing to do here.
    property_set("ctl.oneshot_off", kVsockServiceName);
    useVsock = true;
  }

  LOG(INFO) << "Starting thread pool...";
  if (!ABinderProcess_setThreadPoolMaxThreadCount(4)) {
    LOG(ERROR) << "Failed to set thread pool max thread count.";
    return 1;
  }
  ABinderProcess_startThreadPool();

  unsigned int port =
      static_cast<unsigned int>(property_get_int32(kBootConfigPort, -1));
  CHECK(port >= 0) << "Failed to read port number from: " << kBootConfigPort;

  std::string serverAddr;
  if (useVsock) {
    VsockConnectionInfo vsock = {
        .cid = VMADDR_CID_HOST,
        .port = port,
    };
    serverAddr = vsock.str();
    LOG(INFO) << "Connecting to vsock server at " << serverAddr;
  } else {
    serverAddr = fmt::format("{}:{}", kEthServerAddr, port);
    LOG(INFO) << "Connecting to ethernet server at " << serverAddr;
  }

  constexpr auto maxConnectWaitTime = std::chrono::seconds(5);
  auto hardware = std::make_unique<GRPCVehicleHardware>(serverAddr);
  if (const auto connected = hardware->waitForConnected(maxConnectWaitTime)) {
    LOG(INFO) << "Connected to GRPC server at " << serverAddr;
  } else {
    LOG(INFO)
        << "Failed to connect to GRPC server at " << serverAddr
        << ", check if it is working, or maybe the server is coming up late.";
    return 1;
  }

  auto vhal =
      ::ndk::SharedRefBase::make<DefaultVehicleHal>(std::move(hardware));
  LOG(INFO) << "Registering as service...";
  binder_exception_t err =
      AServiceManager_addService(vhal->asBinder().get(), kServiceName);
  CHECK(err == EX_NONE) << "Failed to register " << kServiceName
                        << " service, exception: " << err << ".";

  LOG(INFO) << "Vehicle Service Ready.";

  ABinderProcess_joinThreadPool();

  LOG(INFO) << "Vehicle Service Exiting, must not happen!.";

  return 0;
}
