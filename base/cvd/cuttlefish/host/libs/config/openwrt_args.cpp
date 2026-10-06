/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/openwrt_args.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "absl/strings/numbers.h"

#include "cuttlefish/host/commands/cvdalloc/interface.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

namespace {

std::string getIpAddress(int c_class, int d_class) {
  return "192.168." + std::to_string(c_class) + "." + std::to_string(d_class);
}

// Network numbers in the fourth hextet of the routed mode prefixes, see
// ipv6_routed_prefix in the cuttlefish-host-resources defaults file
// (base/debian/cuttlefish-base.cuttlefish-host-resources.default).
constexpr uint8_t kRoutedWifiApNetwork = 0x23;
constexpr uint8_t kRoutedWifiLanNetwork = 0x25;

std::optional<std::string> Ipv6ToString(const in6_addr& addr) {
  char buf[INET6_ADDRSTRLEN];
  if (inet_ntop(AF_INET6, &addr, buf, sizeof(buf)) == nullptr) {
    return std::nullopt;
  }
  return std::string(buf);
}

int Ipv6PrefixLength(const in6_addr& netmask) {
  int ret = 0;
  for (uint8_t byte : netmask.s6_addr) {
    for (; byte; byte <<= 1) {
      ret += (byte & 0x80) ? 1 : 0;
    }
  }
  return ret;
}

// Looks for a routed mode address on the host side of the OpenWrt WAN tap.
std::optional<OpenwrtRoutedIpv6Args> ObtainRoutedIpv6Args(
    const std::string& interface, int instance_num) {
  struct ifaddrs* ifa_list = nullptr;
  if (getifaddrs(&ifa_list) != 0) {
    return std::nullopt;
  }
  std::optional<OpenwrtRoutedIpv6Args> ret;
  for (struct ifaddrs* ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
    if (strcmp(ifa->ifa_name, interface.c_str()) != 0 ||
        ifa->ifa_addr == nullptr || ifa->ifa_netmask == nullptr ||
        ifa->ifa_addr->sa_family != AF_INET6) {
      continue;
    }
    const in6_addr& addr =
        reinterpret_cast<const sockaddr_in6*>(ifa->ifa_addr)->sin6_addr;
    const in6_addr& netmask =
        reinterpret_cast<const sockaddr_in6*>(ifa->ifa_netmask)->sin6_addr;
    ret = OpenwrtRoutedIpv6ArgsFromHostAddress(addr, Ipv6PrefixLength(netmask),
                                               instance_num);
    if (ret) {
      break;
    }
  }
  freeifaddrs(ifa_list);
  return ret;
}

}  // namespace

std::optional<OpenwrtRoutedIpv6Args> OpenwrtRoutedIpv6ArgsFromHostAddress(
    const in6_addr& host_addr, int prefix_length, int instance_num) {
  if (prefix_length != 64 || instance_num < 1 || instance_num > 0xff) {
    return std::nullopt;
  }
  const uint8_t* bytes = host_addr.s6_addr;
  // Private mode uses Unique Local Addresses (fc00::/7); routed mode needs a
  // global prefix. Link-local, loopback and multicast are never used.
  if ((bytes[0] & 0xfe) == 0xfc || IN6_IS_ADDR_LINKLOCAL(&host_addr) ||
      IN6_IS_ADDR_LOOPBACK(&host_addr) || IN6_IS_ADDR_MULTICAST(&host_addr)) {
    return std::nullopt;
  }
  // P:23NN::1, where NN is the instance number.
  if (bytes[6] != kRoutedWifiApNetwork || bytes[7] != instance_num) {
    return std::nullopt;
  }
  for (int i = 8; i < 15; ++i) {
    if (bytes[i] != 0) {
      return std::nullopt;
    }
  }
  if (bytes[15] != 1) {
    return std::nullopt;
  }

  in6_addr wan_addr = host_addr;
  wan_addr.s6_addr[15] = 2;
  in6_addr lan_prefix = host_addr;
  lan_prefix.s6_addr[6] = kRoutedWifiLanNetwork;
  lan_prefix.s6_addr[15] = 0;

  std::optional<std::string> wan_addr_str = Ipv6ToString(wan_addr);
  std::optional<std::string> gateway_str = Ipv6ToString(host_addr);
  std::optional<std::string> lan_prefix_str = Ipv6ToString(lan_prefix);
  if (!wan_addr_str || !gateway_str || !lan_prefix_str) {
    return std::nullopt;
  }
  return OpenwrtRoutedIpv6Args{
      .wan_ip6addr = *wan_addr_str + "/64",
      .wan_ip6gw = *gateway_str,
      .lan_ip6prefix = *lan_prefix_str + "/64",
  };
}

std::unordered_map<std::string, std::string> OpenwrtArgsFromConfig(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::unordered_map<std::string, std::string> openwrt_args;
  int instance_num;
  if (!absl::SimpleAtoi(instance.id(), &instance_num) || instance_num < 1 ||
      instance_num > 128) {
    return openwrt_args;
  }
  openwrt_args["webrtc_device_id"] = instance.webrtc_device_id();

  int c_class_base = (instance_num - 1) / 64;
  int d_class_base = (instance_num - 1) % 64 * 4;

  // IP address for OpenWRT is pre-defined in init script of android-cuttlefish
  // github repository by using tap interfaces created with the script.
  // (github) base/debian/cuttlefish-base.cuttlefish-host-resources.init
  // The command 'crosvm run' uses openwrt_args for passing the arguments into
  // /proc/cmdline of OpenWRT instance.
  // (AOSP) device/google/cuttlefish/host/commands/run_cvd/launch/open_wrt.cpp
  // In OpenWRT instance, the script 0_default_config reads /proc/cmdline so
  // that it can apply arguments defined here.
  // (AOSP) external/openwrt-prebuilts/shared/uci-defaults/0_default_config
  if (instance.use_bridged_wifi_tap()) {
    openwrt_args["bridged_wifi_tap"] = "true";

    if (instance.use_cvdalloc()) {
      openwrt_args["wan_gateway"] =
          InstanceToBridgedWifiGatewayAddress(instance_num);
      openwrt_args["wan_ipaddr"] = InstanceToBridgedWifiAddress(instance_num);
      openwrt_args["wan_broadcast"] =
          InstanceToBridgedWifiBroadcast(instance_num);
    } else {
      openwrt_args["wan_gateway"] = getIpAddress(96, 1);
      // TODO(seungjaeyoo) : Remove config after using DHCP server outside
      // OpenWRT instead.
      openwrt_args["wan_ipaddr"] = getIpAddress(96, d_class_base + 2);
      openwrt_args["wan_broadcast"] = getIpAddress(96, d_class_base + 3);
    }
  } else {
    openwrt_args["bridged_wifi_tap"] = "false";

    if (instance.use_cvdalloc()) {
      openwrt_args["wan_gateway"] = InstanceToWifiGatewayAddress(instance_num);
      openwrt_args["wan_ipaddr"] = InstanceToWifiAddress(instance_num);
      openwrt_args["wan_broadcast"] = InstanceToWifiBroadcast(instance_num);
    } else {
      openwrt_args["wan_gateway"] =
          getIpAddress(94 + c_class_base, d_class_base + 1);
      openwrt_args["wan_ipaddr"] =
          getIpAddress(94 + c_class_base, d_class_base + 2);
      openwrt_args["wan_broadcast"] =
          getIpAddress(94 + c_class_base, d_class_base + 3);

      // IPv6 routed mode: the host init script gives cvd-wifiap-NN an address
      // from the routed /48, so the routed mode is detected from that address
      // with no extra flag. In private mode no IPv6 keys are passed and
      // OpenWrt keeps its default (ULA LAN prefix and masq6).
      std::optional<OpenwrtRoutedIpv6Args> ipv6 =
          ObtainRoutedIpv6Args(instance.wifi_tap_name(), instance_num);
      if (ipv6) {
        openwrt_args["wan_ip6addr"] = ipv6->wan_ip6addr;
        openwrt_args["wan_ip6gw"] = ipv6->wan_ip6gw;
        openwrt_args["lan_ip6prefix"] = ipv6->lan_ip6prefix;
      }
    }
  }

  return openwrt_args;
}

}  // namespace cuttlefish
