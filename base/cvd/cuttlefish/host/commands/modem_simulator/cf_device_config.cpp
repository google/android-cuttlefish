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

#include "host/commands/modem_simulator/device_config.h"
#include "host/libs/config/cuttlefish_config.h"

// this file provide cuttlefish hooks
namespace cuttlefish {
namespace modem {

int DeviceConfig::host_id() {
  if (!cuttlefish::CuttlefishConfig::Get()) {
    return 1000;
  }
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  return instance.modem_simulator_host_id();
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

std::string DeviceConfig::ril_address_and_prefix() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  return instance.ril_ipaddr() + "/" + std::to_string(instance.ril_prefixlen());
};

std::string DeviceConfig::ril_gateway() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  return instance.ril_gateway();
}

std::string DeviceConfig::ril_dns() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  return instance.ril_dns();
}

std::ifstream DeviceConfig::open_ifstream_crossplat(const char* filename) {
    return std::ifstream(filename);
}

std::ofstream DeviceConfig::open_ofstream_crossplat(const char* filename, std::ios_base::openmode mode) {
    return std::ofstream(filename, mode);
}

}  // namespace modem
}  // namespace cuttlefish
