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

#include <cassert>
#include <string>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/vsoc/lib/wifi_exchange_view.h"
#include "host/commands/launch/pre_launch_initializers.h"
#include "host/libs/config/host_config.h"

using vsoc::wifi::WifiExchangeView;

namespace {

std::string GetPerInstanceDefaultMacAddress(const char* base_mac) {
  WifiExchangeView::MacAddress addr;
  if (!WifiExchangeView::ParseMACAddress(base_mac, &addr)) {
    LOG(FATAL) << "Unable to parse MAC address: " << base_mac;
    return "";
  }
  // Modify the last byte of the mac address to make it different for every cvd
  addr.back() = static_cast<uint8_t>(vsoc::GetPerInstanceDefault(addr.back()));
  return WifiExchangeView::MacAddressToString(addr);
}

}  // namespace

DEFINE_string(guest_mac_address,
              GetPerInstanceDefaultMacAddress("00:43:56:44:80:01"),
              "MAC address of the wifi interface to be created on the guest.");

DEFINE_string(host_mac_address,
              "42:00:00:00:00:00",
              "MAC address of the wifi interface running on the host.");

void InitializeWifiRegion() {
  auto region = WifiExchangeView::GetInstance(vsoc::GetDomain().c_str());
  if (!region) {
    LOG(FATAL) << "Wifi region not found";
    return;
  }
  WifiExchangeView::MacAddress guest_mac, host_mac;
  if (!WifiExchangeView::ParseMACAddress(FLAGS_guest_mac_address, &guest_mac)) {
    LOG(FATAL) << "Unable to parse guest mac address: "
               << FLAGS_guest_mac_address;
    return;
  }
  LOG(INFO) << "Setting guest mac to " << FLAGS_guest_mac_address;
  region->SetGuestMACAddress(guest_mac);
  if (!WifiExchangeView::ParseMACAddress(FLAGS_host_mac_address, &host_mac)) {
    LOG(FATAL) << "Unable to parse guest mac address: "
               << FLAGS_guest_mac_address;
    return;
  }
  LOG(INFO) << "Setting host mac to " << FLAGS_host_mac_address;
  region->SetHostMACAddress(host_mac);
}
