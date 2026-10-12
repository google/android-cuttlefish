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

#include "cuttlefish/host/libs/config/openwrt_args.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>

#include <optional>
#include <string>

namespace cuttlefish {
namespace {

in6_addr Addr(const std::string& str) {
  in6_addr addr{};
  EXPECT_EQ(inet_pton(AF_INET6, str.c_str(), &addr), 1) << str;
  return addr;
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, FirstInstance) {
  std::optional<OpenwrtRoutedIpv6Args> args =
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("2001:db8:cf00:2301::1"), 64,
                                           1);
  ASSERT_TRUE(args.has_value());
  EXPECT_EQ(args->wan_ip6addr, "2001:db8:cf00:2301::2/64");
  EXPECT_EQ(args->wan_ip6gw, "2001:db8:cf00:2301::1");
  EXPECT_EQ(args->lan_ip6prefix, "2001:db8:cf00:2501::/64");
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, ShortRoutedPrefix) {
  // ipv6_routed_prefix=2001:db8::/48 gives P = 2001:db8:0.
  std::optional<OpenwrtRoutedIpv6Args> args =
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("2001:db8:0:230a::1"), 64, 10);
  ASSERT_TRUE(args.has_value());
  EXPECT_EQ(args->wan_ip6addr, "2001:db8:0:230a::2/64");
  EXPECT_EQ(args->wan_ip6gw, "2001:db8:0:230a::1");
  EXPECT_EQ(args->lan_ip6prefix, "2001:db8:0:250a::/64");
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, AllInstances) {
  // The host init script sets up instances 1 to 128.
  for (int i = 1; i <= 128; ++i) {
    char host[64];
    char wan[64];
    char lan[64];
    snprintf(host, sizeof(host), "2001:db8:cf00:23%02x::1", i);
    snprintf(wan, sizeof(wan), "2001:db8:cf00:23%02x::2/64", i);
    snprintf(lan, sizeof(lan), "2001:db8:cf00:25%02x::/64", i);
    std::optional<OpenwrtRoutedIpv6Args> args =
        OpenwrtRoutedIpv6ArgsFromHostAddress(Addr(host), 64, i);
    ASSERT_TRUE(args.has_value()) << host;
    EXPECT_EQ(args->wan_ip6addr, wan);
    EXPECT_EQ(args->wan_ip6gw, host);
    EXPECT_EQ(args->lan_ip6prefix, lan);
  }
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, PrivateModeAddressIsIgnored) {
  // Private mode: fd00:cf:23:<instance>::1/64, a ULA.
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("fd00:cf:23:1::1"), 64, 1)
          .has_value());
  // A ULA with the routed layout is still private mode.
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("fd00:cf:0:2301::1"), 64, 1)
          .has_value());
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("fc00:cf:0:2301::1"), 64, 1)
          .has_value());
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, CustomGlobalBaseIsIgnored) {
  // wifiap_ipv6_prefix_base=2001:db8:23 in private mode gives
  // 2001:db8:23:<instance>::1, which is not the routed layout.
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("2001:db8:23:1::1"), 64, 1)
          .has_value());
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, WrongInstance) {
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("2001:db8:cf00:2302::1"), 64, 1)
          .has_value());
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, WrongNetwork) {
  // P:21NN::1 is the mobile network, not the OpenWrt WAN.
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("2001:db8:cf00:2101::1"), 64, 1)
          .has_value());
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, NotHostAddress) {
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("2001:db8:cf00:2301::2"), 64, 1)
          .has_value());
  EXPECT_FALSE(OpenwrtRoutedIpv6ArgsFromHostAddress(
                   Addr("2001:db8:cf00:2301:1::1"), 64, 1)
                   .has_value());
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, WrongPrefixLength) {
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("2001:db8:cf00:2301::1"), 48, 1)
          .has_value());
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("2001:db8:cf00:2301::1"), 96, 1)
          .has_value());
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, LinkLocalIsIgnored) {
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("fe80::2301:0:0:1"), 64, 1)
          .has_value());
}

TEST(OpenwrtRoutedIpv6ArgsFromHostAddress, InvalidInstance) {
  EXPECT_FALSE(
      OpenwrtRoutedIpv6ArgsFromHostAddress(Addr("2001:db8:cf00:2300::1"), 64, 0)
          .has_value());
}

}  // namespace
}  // namespace cuttlefish
