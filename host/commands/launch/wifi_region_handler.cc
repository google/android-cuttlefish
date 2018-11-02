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

#include <string>

#include <glog/logging.h>

#include "common/vsoc/lib/wifi_exchange_view.h"
#include "host/commands/launch/pre_launch_initializers.h"
#include "host/libs/config/cuttlefish_config.h"

using vsoc::wifi::WifiExchangeView;

void InitializeWifiRegion(const vsoc::CuttlefishConfig& config) {
  auto region = WifiExchangeView::GetInstance(vsoc::GetDomain().c_str());
  if (!region) {
    LOG(FATAL) << "Wifi region not found";
    return;
  }
  WifiExchangeView::MacAddress guest_mac, host_mac;
  if (!WifiExchangeView::ParseMACAddress(config.wifi_guest_mac_addr(),
                                         &guest_mac)) {
    LOG(FATAL) << "Unable to parse guest mac address: "
               << config.wifi_guest_mac_addr();
    return;
  }
  LOG(INFO) << "Setting guest mac to " << config.wifi_guest_mac_addr();
  region->SetGuestMACAddress(guest_mac);
  if (!WifiExchangeView::ParseMACAddress(config.wifi_host_mac_addr(),
                                         &host_mac)) {
    LOG(FATAL) << "Unable to parse guest mac address: "
               << config.wifi_guest_mac_addr();
    return;
  }
  LOG(INFO) << "Setting host mac to " << config.wifi_host_mac_addr();
  region->SetHostMACAddress(host_mac);
}
