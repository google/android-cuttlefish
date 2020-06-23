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

#include <cutils/properties.h>
#include <android-base/logging.h>

namespace cuttlefish {

namespace {

static constexpr auto kDataSize = sizeof(DeviceConfig::RawData);
static constexpr int kRetries = 5;
static constexpr int kRetryDelaySeconds = 5;

bool GetRawFromServer(DeviceConfig::RawData* data) {
  auto port_property = "ro.boot.cuttlefish_config_server_port";
  auto port = property_get_int64(port_property, -1);
  if (port < 0) {
    LOG(ERROR) << "Unable to get config server port from property: " <<
        port_property;
    return false;
  }
  auto config_server =
      cuttlefish::SharedFD::VsockClient(2 /*host cid*/,
                                 static_cast<unsigned int>(port), SOCK_STREAM);
  if (!config_server->IsOpen()) {
    LOG(ERROR) << "Unable to connect to config server: "
               << config_server->StrError();
    return false;
  }
  uint8_t* buffer = reinterpret_cast<uint8_t*>(data);
  size_t read_idx = 0;
  while (read_idx < kDataSize) {
    auto read = config_server->Read(buffer + read_idx, kDataSize - read_idx);
    if (read == 0) {
      LOG(ERROR) << "Unexpected EOF while reading from config server, read "
                 << read_idx << " bytes, expected " << kDataSize;
      return false;
    }
    if (read < 0) {
      LOG(ERROR) << "Error reading from config server: "
                 << config_server->StrError();
      return false;
    }
    read_idx += read;
  }
  return true;
}

}  // namespace

std::unique_ptr<DeviceConfig> DeviceConfig::Get() {
  DeviceConfig::RawData data;

  int attempts_remaining = 1 + kRetries;
  while (attempts_remaining > 0) {
    if (GetRawFromServer(&data)) {
      return std::unique_ptr<DeviceConfig>(new DeviceConfig(data));
    }

    std::this_thread::sleep_for(std::chrono::seconds(kRetryDelaySeconds));

    --attempts_remaining;
  }
  return nullptr;
}

DeviceConfig::DeviceConfig(const DeviceConfig::RawData& data) : data_(data) {
  generate_address_and_prefix();
}

}  // namespace cuttlefish
