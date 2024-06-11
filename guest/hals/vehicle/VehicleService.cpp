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

const char* SERVICE_NAME =
    "android.hardware.automotive.vehicle.IVehicle/default";
const char* BOOTCONFIG_PORT = "ro.boot.vhal_proxy_server_port";

int main(int /* argc */, char* /* argv */[]) {
  LOG(INFO) << "Starting thread pool...";
  if (!ABinderProcess_setThreadPoolMaxThreadCount(4)) {
    LOG(ERROR) << "Failed to set thread pool max thread count.";
    return 1;
  }
  ABinderProcess_startThreadPool();

  VsockConnectionInfo vsock = {
      .cid = VMADDR_CID_HOST,
      .port =
          static_cast<unsigned int>(property_get_int32(BOOTCONFIG_PORT, -1)),
  };
  CHECK(vsock.port >= 0) << "Failed to read port number from: "
                         << BOOTCONFIG_PORT;
  std::string vsockStr = vsock.str();

  LOG(INFO) << "Connecting to vsock server at " << vsockStr;

  constexpr auto maxConnectWaitTime = std::chrono::seconds(5);
  auto hardware = std::make_unique<GRPCVehicleHardware>(vsockStr);
  if (const auto connected = hardware->waitForConnected(maxConnectWaitTime)) {
    LOG(INFO) << "Connected to vsock server at " << vsockStr;
  } else {
    LOG(INFO)
        << "Failed to connect to vsock server at " << vsockStr
        << ", check if it is working, or maybe the server is coming up late.";
    return 1;
  }

  auto vhal =
      ::ndk::SharedRefBase::make<DefaultVehicleHal>(std::move(hardware));
  LOG(INFO) << "Registering as service...";
  binder_exception_t err =
      AServiceManager_addService(vhal->asBinder().get(), SERVICE_NAME);
  CHECK(err == EX_NONE) << "Failed to register " << SERVICE_NAME
                        << " service, exception: " << err << ".";

  LOG(INFO) << "Vehicle Service Ready.";

  ABinderProcess_joinThreadPool();

  LOG(INFO) << "Vehicle Service Exiting.";

  return 0;
}
