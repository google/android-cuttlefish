/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/ipv6_utils.h"

#include <arpa/inet.h>
#include <sys/socket.h>

namespace cuttlefish {

uint8_t Ipv6PrefixLength(const in6_addr& netmask) {
  uint8_t ret = 0;
  for (uint8_t byte : netmask.s6_addr) {
    for (; byte; byte <<= 1) {
      ret += (byte & 0x80) ? 1 : 0;
    }
  }
  return ret;
}

std::string Ipv6ToString(const in6_addr& addr) {
  char buf[INET6_ADDRSTRLEN] = {};
  inet_ntop(AF_INET6, &addr, buf, sizeof(buf));
  return std::string(buf);
}

void IncrementIpv6Address(in6_addr& addr) {
  for (int i = 15; i >= 0; --i) {
    if (++addr.s6_addr[i] != 0) {
      return;
    }
  }
}

}  // namespace cuttlefish
