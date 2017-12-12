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

#include "common/vsoc/lib/circqueue_impl.h"

namespace vsoc {
namespace wifi {

bool WifiExchangeView::Send(const void* buffer, size_t length) {
#ifdef CUTTLEFISH_HOST
  return data()->guest_ingress.Write(this, static_cast<const char*>(buffer),
                                     length) == length;
#else
  return data()->guest_egress.Write(this, static_cast<const char*>(buffer),
                                    length) == length;
#endif
}

intptr_t WifiExchangeView::Recv(void* buffer, size_t max_length) {
#ifdef CUTTLEFISH_HOST
  return data()->guest_egress.Read(this, static_cast<char*>(buffer),
                                   max_length);
#else
  return data()->guest_ingress.Read(this, static_cast<char*>(buffer),
                                    max_length);
#endif
}

void WifiExchangeView::SetGuestMACAddress(const uint8_t* mac_address) {
  memcpy(data()->mac_address, mac_address, ETH_ALEN);
}

void WifiExchangeView::GetGuestMACAddress(uint8_t* mac_address) {
  memcpy(mac_address, data()->mac_address, ETH_ALEN);
}

}  // namespace wifi
}  // namespace vsoc
