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

#include <android-base/logging.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "device_config.h"

namespace cuttlefish {

namespace {

bool InitializeNetworkConfiguration(const CuttlefishConfig& cuttlefish_config,
                                    DeviceConfig* device_config) {
  auto instance = cuttlefish_config.ForDefaultInstance();

  DeviceConfig::RILConfig* ril_config = device_config->mutable_ril_config();
  ril_config->set_ipaddr(instance.ril_ipaddr());
  ril_config->set_gateway(instance.ril_gateway());
  ril_config->set_dns(instance.ril_dns());
  ril_config->set_broadcast(instance.ril_broadcast());
  ril_config->set_prefixlen(instance.ril_prefixlen());

  return true;
}

void InitializeScreenConfiguration(const CuttlefishConfig& cuttlefish_config,
                                   DeviceConfig* device_config) {
  auto instance = cuttlefish_config.ForDefaultInstance();
  for (const auto& cuttlefish_display_config : instance.display_configs()) {
    DeviceConfig::DisplayConfig* device_display_config =
      device_config->add_display_config();

    device_display_config->set_width(cuttlefish_display_config.width);
    device_display_config->set_height(cuttlefish_display_config.height);
    device_display_config->set_dpi(cuttlefish_display_config.dpi);
    device_display_config->set_refresh_rate_hz(
        cuttlefish_display_config.refresh_rate_hz);
  }
}

}  // namespace

std::unique_ptr<DeviceConfigHelper> DeviceConfigHelper::Get() {
  auto cuttlefish_config = CuttlefishConfig::Get();
  if (!cuttlefish_config) {
    return nullptr;
  }

  DeviceConfig device_config;
  if (!InitializeNetworkConfiguration(*cuttlefish_config, &device_config)) {
    return nullptr;
  }
  InitializeScreenConfiguration(*cuttlefish_config, &device_config);

  return std::unique_ptr<DeviceConfigHelper>(
    new DeviceConfigHelper(device_config));
}

}  // namespace cuttlefish
