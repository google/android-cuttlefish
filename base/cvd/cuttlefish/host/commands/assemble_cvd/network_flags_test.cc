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

#include "cuttlefish/host/commands/assemble_cvd/network_flags.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
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

TEST(MobileIpv6ConfigFromHostAddress, HostIsFirstAddress) {
  std::optional<MobileIpv6Config> config = MobileIpv6ConfigFromHostAddress(
      Addr("fd00:cf:21:1::1"), Addr("ffff:ffff:ffff:ffff::"));
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->ipaddr, "fd00:cf:21:1::2");
  EXPECT_EQ(config->gateway, "fd00:cf:21:1::1");
  EXPECT_EQ(config->prefixlen, 64);
}

TEST(MobileIpv6ConfigFromHostAddress, HexInstanceNumber) {
  std::optional<MobileIpv6Config> config = MobileIpv6ConfigFromHostAddress(
      Addr("fd00:cf:21:1a::1"), Addr("ffff:ffff:ffff:ffff::"));
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->ipaddr, "fd00:cf:21:1a::2");
  EXPECT_EQ(config->gateway, "fd00:cf:21:1a::1");
}

TEST(MobileIpv6ConfigFromHostAddress, HostIsNotFirstAddress) {
  std::optional<MobileIpv6Config> config = MobileIpv6ConfigFromHostAddress(
      Addr("fd00:cf:21:2::5"), Addr("ffff:ffff:ffff:ffff::"));
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->ipaddr, "fd00:cf:21:2::1");
  EXPECT_EQ(config->gateway, "fd00:cf:21:2::5");
}

TEST(MobileIpv6ConfigFromHostAddress, NoRoomInPrefix) {
  // A /128 leaves no address for the guest.
  EXPECT_FALSE(MobileIpv6ConfigFromHostAddress(
                   Addr("fd00:cf:21:1::1"),
                   Addr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"))
                   .has_value());
  // In a /127 whose second address is the host, the only other address is
  // the network address, which is not used.
  EXPECT_FALSE(MobileIpv6ConfigFromHostAddress(
                   Addr("fd00:cf:21:1::1"),
                   Addr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe"))
                   .has_value());
}

}  // namespace
}  // namespace cuttlefish
