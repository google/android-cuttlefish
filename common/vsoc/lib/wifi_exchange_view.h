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
#pragma once

#include <array>
#include <memory>

#include "common/vsoc/lib/typed_region_view.h"
#include "common/vsoc/shm/wifi_exchange_layout.h"
#include "uapi/vsoc_shm.h"

namespace vsoc {
namespace wifi {

class WifiExchangeView
    : public vsoc::TypedRegionView<
        WifiExchangeView,
        vsoc::layout::wifi::WifiExchangeLayout> {
 public:
  using MacAddress = std::array<
      uint8_t,
      sizeof(vsoc::layout::wifi::WifiExchangeLayout::guest_mac_address)>;

  // Send netlink packet to peer.
  // returns true, if operation was successful.
  intptr_t Send(const void* buffer, intptr_t length);

  // Receive netlink packet from peer.
  // Returns number of bytes read, or negative value, if failed.
  intptr_t Recv(void* buffer, intptr_t max_length);

  // Set guest MAC address.
  void SetGuestMACAddress(const MacAddress& mac_address);
  MacAddress GetGuestMACAddress();

  // Set host MAC address.
  void SetHostMACAddress(const MacAddress& mac_address);
  MacAddress GetHostMACAddress();

  void SetConfigReady();
  void WaitConfigReady();

  static bool ParseMACAddress(const std::string &s, MacAddress *mac);
  static std::string MacAddressToString(const MacAddress& mac);
};

}  // namespace wifi
}  // namespace vsoc
