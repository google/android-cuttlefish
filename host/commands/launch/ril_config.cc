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
#include <ifaddrs.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <sstream>
#include <string>

#include <glog/logging.h>

#include "common/libs/constants/ril.h"
#include "host/commands/launch/ril_config.h"

namespace {

int number_of_ones(unsigned long val) {
  int ret = 0;
  while (val) {
    ret += val % 2;
    val >>= 1;
  }
  return ret;
}

class NetConfig {
 public:
  uint32_t ril_prefixlen = -1;
  std::string ril_ipaddr;
  std::string ril_gateway;
  std::string ril_dns = "8.8.8.8";
  std::string ril_broadcast;

  bool ObtainConfig(const std::string& interface) {
    bool ret = ParseIntefaceAttributes(interface);
    LOG(INFO) << "Network config:";
    LOG(INFO) << "ipaddr = " << ril_ipaddr;
    LOG(INFO) << "gateway = " << ril_gateway;
    LOG(INFO) << "dns = " << ril_dns;
    LOG(INFO) << "broadcast = " << ril_broadcast;
    LOG(INFO) << "prefix length = " << ril_prefixlen;
    return ret;
  }

 private:
  bool ParseIntefaceAttributes(struct ifaddrs* ifa) {
    // if (ifa->ifa_addr->sa_family != AF_INET) {
    //   LOG(ERROR) << "The " << ifa->ifa_name << " interface is not IPv4";
    //   return false;
    // }
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

  bool ParseIntefaceAttributes(const std::string& interface) {
    struct ifaddrs *ifa_list{}, *ifa{};
    bool ret = false;
    getifaddrs(&ifa_list);
    for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
      if (strcmp(ifa->ifa_name, interface.c_str()) == 0 &&
          ifa->ifa_addr->sa_family == AF_INET) {
        ret = ParseIntefaceAttributes(ifa);
        break;
      }
    }
    freeifaddrs(ifa_list);
    return ret;
  }
};

template <typename T>
std::string BuildPropertyDefinition(const std::string& prop_name,
                                  const T& prop_value) {
  std::ostringstream stream;
  stream << prop_name << "=" << prop_value;
  return stream.str();
}
}  // namespace

void ConfigureRil(vsoc::CuttlefishConfig* config) {
  NetConfig netconfig;
  if (!netconfig.ObtainConfig(config->mobile_bridge_name())) {
    LOG(ERROR) << "Unable to obtain the network configuration";
    return;
  }

  config->add_kernel_cmdline(BuildPropertyDefinition(
      CUTTLEFISH_RIL_ADDR_PROPERTY, netconfig.ril_ipaddr));
  config->add_kernel_cmdline(BuildPropertyDefinition(
      CUTTLEFISH_RIL_GATEWAY_PROPERTY, netconfig.ril_gateway));
  config->add_kernel_cmdline(BuildPropertyDefinition(
      CUTTLEFISH_RIL_DNS_PROPERTY, netconfig.ril_dns));
  config->add_kernel_cmdline(BuildPropertyDefinition(
      CUTTLEFISH_RIL_BROADCAST_PROPERTY, netconfig.ril_broadcast));
  config->add_kernel_cmdline(BuildPropertyDefinition(
      CUTTLEFISH_RIL_PREFIXLEN_PROPERTY, netconfig.ril_prefixlen));
}
