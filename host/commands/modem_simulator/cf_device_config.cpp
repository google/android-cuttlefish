/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "common/libs/device_config/device_config.h"
#include "host/commands/modem_simulator/device_config.h"
#include "host/libs/config/cuttlefish_config.h"

// this file provide cuttlefish hooks
namespace cuttlefish {
namespace modem {

int DeviceConfig::host_port() {
  if (!cuttlefish::CuttlefishConfig::Get()) {
      return 6500;
  }
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  auto host_port = instance.host_port();
  return host_port;
}

std::string DeviceConfig::PerInstancePath(const char* file_name) {
  if (!cuttlefish::CuttlefishConfig::Get()) {
      return "";
  }
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  return instance.PerInstancePath(file_name);
}

std::string DeviceConfig::DefaultHostArtifactsPath(const std::string& file) {
  return cuttlefish::DefaultHostArtifactsPath(file);
}

const char* DeviceConfig::ril_address_and_prefix() {
  if (!cuttlefish::DeviceConfig::Get()) {
      return "10.0.2.15/24";
  }
  return cuttlefish::DeviceConfig::Get()->ril_address_and_prefix();
};

const char* DeviceConfig::ril_gateway() {
  if (!cuttlefish::DeviceConfig::Get()) {
      return "10.0.2.2";
  }
  return cuttlefish::DeviceConfig::Get()->ril_gateway();
}
const char* DeviceConfig::ril_dns() {
  if (!cuttlefish::DeviceConfig::Get()) {
      return "8.8.8.8";
  }
  return cuttlefish::DeviceConfig::Get()->ril_dns();
}

}  // namespace modem
}  // namespace cuttlefish
