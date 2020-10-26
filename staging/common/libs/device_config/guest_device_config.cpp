/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "device_config.h"

#include <chrono>
#include <thread>

#include <android-base/logging.h>
#include <cutils/properties.h>

#include "common/libs/fs/shared_fd_stream.h"

namespace cuttlefish {

namespace {

static constexpr int kRetries = 5;
static constexpr int kRetryDelaySeconds = 5;

bool GetRawFromServer(DeviceConfig* data) {
  auto port_property = "ro.boot.cuttlefish_config_server_port";
  auto port = property_get_int64(port_property, -1);
  if (port < 0) {
    LOG(ERROR) << "Unable to get config server port from property: " <<
        port_property;
    return false;
  }
  auto config_server =
      SharedFD::VsockClient(2 /*host cid*/,
                            static_cast<unsigned int>(port), SOCK_STREAM);
  if (!config_server->IsOpen()) {
    LOG(ERROR) << "Unable to connect to config server: "
               << config_server->StrError();
    return false;
  }

  SharedFDIstream stream(config_server);
  if (!data->ParseFromIstream(&stream)) {
    LOG(ERROR) << "Error reading from config server: "
               << config_server->StrError();
  }
  return true;
}

}  // namespace

std::unique_ptr<DeviceConfigHelper> DeviceConfigHelper::Get() {
  DeviceConfig device_config;

  int attempts_remaining = 1 + kRetries;
  while (attempts_remaining > 0) {
    if (GetRawFromServer(&device_config)) {
      return std::unique_ptr<DeviceConfigHelper>(
        new DeviceConfigHelper(device_config));
    }

    std::this_thread::sleep_for(std::chrono::seconds(kRetryDelaySeconds));

    --attempts_remaining;
  }
  return nullptr;
}

}  // namespace cuttlefish
