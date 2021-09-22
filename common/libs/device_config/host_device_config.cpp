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

#include <arpa/inet.h>
#include <android-base/logging.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "device_config.h"

namespace cvd {

namespace {

uint8_t number_of_ones(unsigned long val) {
  uint8_t ret = 0;
  while (val) {
    ret += val % 2;
    val >>= 1;
  }
  return ret;
}

class NetConfig {
 public:
  uint8_t ril_prefixlen = -1;
  std::string ril_ipaddr;
  std::string ril_gateway;
  std::string ril_dns = "8.8.8.8";
  std::string ril_broadcast;

  bool ObtainConfig(const std::string& interface) {
    bool ret = ParseInterfaceAttributes(interface);
    if (ret) {
      LOG(INFO) << "Network config:";
      LOG(INFO) << "ipaddr = " << ril_ipaddr;
      LOG(INFO) << "gateway = " << ril_gateway;
      LOG(INFO) << "dns = " << ril_dns;
      LOG(INFO) << "broadcast = " << ril_broadcast;
      LOG(INFO) << "prefix length = " << static_cast<int>(ril_prefixlen);
    }
    return ret;
  }

 private:
  bool ParseInterfaceAttributes(struct ifaddrs* ifa) {
    struct sockaddr_in* sa;
    char* addr_str;

    // Gateway
    sa = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
    addr_str = inet_ntoa(sa->sin_addr);
    this->ril_gateway = strtok(addr_str, "\n");
    auto gateway_s_addr = ntohl(sa->sin_addr.s_addr);

    // Broadcast
    sa = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
    addr_str = inet_ntoa(sa->sin_addr);
    this->ril_broadcast = strtok(addr_str, "\n");
    auto broadcast_s_addr = ntohl(sa->sin_addr.s_addr);

    // Detect misconfigured network interfaces. All network interfaces must
    // have a valid broadcast address set; if there is none set, glibc may
    // return the interface address in the broadcast field. This causes
    // no packets to be routed correctly from the guest.
    if (this->ril_gateway == this->ril_broadcast) {
      LOG(ERROR) << "Gateway and Broadcast addresses are the same on "
                 << ifa->ifa_name << ", which is invalid.";
      return false;
    }

    // Netmask
    sa = reinterpret_cast<sockaddr_in*>(ifa->ifa_netmask);
    this->ril_prefixlen = number_of_ones(sa->sin_addr.s_addr);
    auto netmask_s_addr = ntohl(sa->sin_addr.s_addr);

    // Address (Find an address in the network different than the network, the
    // gateway and the broadcast)
    auto network = gateway_s_addr & netmask_s_addr;
    auto s_addr = network + 1;
    // s_addr & ~netmask_s_addr is zero when s_addr wraps around the network
    while (s_addr & ~netmask_s_addr) {
      if (s_addr != gateway_s_addr && s_addr != broadcast_s_addr) {
        break;
      }
      ++s_addr;
    }
    if (s_addr == network) {
      LOG(ERROR) << "No available address found in interface " << ifa->ifa_name;
      return false;
    }
    struct in_addr addr;
    addr.s_addr = htonl(s_addr);
    addr_str = inet_ntoa(addr);
    this->ril_ipaddr = strtok(addr_str, "\n");
    return true;
  }

  bool ParseInterfaceAttributes(const std::string& interface) {
    struct ifaddrs *ifa_list{}, *ifa{};
    bool ret = false;
    getifaddrs(&ifa_list);
    for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
      if (strcmp(ifa->ifa_name, interface.c_str()) == 0 &&
          ifa->ifa_addr->sa_family == AF_INET) {
        ret = ParseInterfaceAttributes(ifa);
        break;
      }
    }
    freeifaddrs(ifa_list);
    return ret;
  }
};

inline void CopyChars(char* dest, size_t size, const char* src) {
  auto res = snprintf(dest, size, "%s", src);
  if (res >= static_cast<int>(size)) {
    LOG(ERROR) << "Longer(" << res << ") than expected(" << (size - 1)
               << ") config string was truncated: " << dest;
  }
}

}  // namespace

std::unique_ptr<DeviceConfig> DeviceConfig::Get() {
  auto config = vsoc::CuttlefishConfig::Get();
  if (!config) return nullptr;
  std::unique_ptr<DeviceConfig> dev_config(new DeviceConfig());
  if (!dev_config->InitializeNetworkConfiguration(*config)) {
    return nullptr;
  }
  dev_config->InitializeScreenConfiguration(*config);
  return dev_config;
}

bool DeviceConfig::InitializeNetworkConfiguration(
    const vsoc::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  NetConfig netconfig;
  // Check the mobile bridge first; this was the traditional way we configured
  // the mobile interface. If that fails, it probably means we are using a
  // newer version of cuttlefish-common, and we can use the tap device
  // directly instead.
  if (!netconfig.ObtainConfig(instance.mobile_bridge_name())) {
    if (!netconfig.ObtainConfig(instance.mobile_tap_name())) {
      LOG(ERROR) << "Unable to obtain the network configuration";
      return false;
    }
  }

  auto res = snprintf(data_.ril.ipaddr, sizeof(data_.ril.ipaddr), "%s",
                      netconfig.ril_ipaddr.c_str());
  if (res >= (int)sizeof(data_.ril.ipaddr)) {
    LOG(ERROR) << "Longer than expected config string was truncated: "
               << data_.ril.ipaddr;
  }
  CopyChars(data_.ril.gateway, sizeof(data_.ril.gateway),
            netconfig.ril_gateway.c_str());
  CopyChars(data_.ril.dns, sizeof(data_.ril.dns), netconfig.ril_dns.c_str());
  CopyChars(data_.ril.broadcast, sizeof(data_.ril.broadcast),
            netconfig.ril_broadcast.c_str());
  data_.ril.prefixlen = netconfig.ril_prefixlen;

  generate_address_and_prefix();

  return true;
}

void DeviceConfig::InitializeScreenConfiguration(
    const vsoc::CuttlefishConfig& config) {
  data_.screen.x_res = config.x_res();
  data_.screen.y_res = config.y_res();
  data_.screen.dpi = config.dpi();
  data_.screen.refresh_rate = config.refresh_rate_hz();
}

}  // namespace cvd
