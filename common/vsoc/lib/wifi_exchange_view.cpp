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
#include "common/vsoc/lib/wifi_exchange_view.h"

#include <algorithm>
#include <string>

#include <linux/if_ether.h>
#include "common/vsoc/lib/circqueue_impl.h"

namespace vsoc {
namespace wifi {

intptr_t WifiExchangeView::Send(const void* buffer, intptr_t length) {
#ifdef CUTTLEFISH_HOST
  return data()->guest_ingress.Write(this, static_cast<const char*>(buffer),
                                     length);
#else
  return data()->guest_egress.Write(this, static_cast<const char*>(buffer),
                                    length);
#endif
}

intptr_t WifiExchangeView::Recv(void* buffer, intptr_t max_length) {
#ifdef CUTTLEFISH_HOST
  return data()->guest_egress.Read(this, static_cast<char*>(buffer),
                                   max_length);
#else
  return data()->guest_ingress.Read(this, static_cast<char*>(buffer),
                                    max_length);
#endif
}

void WifiExchangeView::SetGuestMACAddress(
    const WifiExchangeView::MacAddress& mac_address) {
  std::copy(std::begin(mac_address),
            std::end(mac_address),
            std::begin(data()->guest_mac_address));
}

WifiExchangeView::MacAddress WifiExchangeView::GetGuestMACAddress() {
  WifiExchangeView::MacAddress ret;
  std::copy(std::begin(data()->guest_mac_address),
            std::end(data()->guest_mac_address),
            std::begin(ret));
  return ret;
}

void WifiExchangeView::SetHostMACAddress(
    const WifiExchangeView::MacAddress& mac_address) {
  std::copy(std::begin(mac_address),
            std::end(mac_address),
            std::begin(data()->host_mac_address));
}

WifiExchangeView::MacAddress WifiExchangeView::GetHostMACAddress() {
  WifiExchangeView::MacAddress ret;
  std::copy(std::begin(data()->host_mac_address),
            std::end(data()->host_mac_address),
            std::begin(ret));
  return ret;
}

// static
bool WifiExchangeView::ParseMACAddress(const std::string& s,
                                       WifiExchangeView::MacAddress* mac) {
  char dummy;
  // This is likely to always be true, but better safe than sorry
  static_assert(std::tuple_size<WifiExchangeView::MacAddress>::value == 6,
                "Mac address size has changed");
  if (sscanf(s.c_str(),
             "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx%c",
             &(*mac)[0],
             &(*mac)[1],
             &(*mac)[2],
             &(*mac)[3],
             &(*mac)[4],
             &(*mac)[5],
             &dummy) != 6) {
    return false;
  }
  return true;
}

// static
std::string WifiExchangeView::MacAddressToString(
    const WifiExchangeView::MacAddress& mac) {
  char buffer[3 * mac.size()];
  // This is likely to always be true, but better safe than sorry
  static_assert(std::tuple_size<WifiExchangeView::MacAddress>::value == 6,
                "Mac address size has changed");
  snprintf(buffer,
           sizeof(buffer),
           "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0],
           mac[1],
           mac[2],
           mac[3],
           mac[4],
           mac[5]);
  return std::string(buffer);
}

}  // namespace wifi
}  // namespace vsoc
