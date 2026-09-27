//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/modem_simulator/data_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace cuttlefish {
namespace {

using ::testing::ElementsAre;

const DataService::IpParams kIpv4 = {
    .address_and_prefix = "192.168.97.2/30",
    .gateways = "192.168.97.1",
    .dnses = "8.8.8.8",
};
const DataService::IpParams kIpv6 = {
    .address_and_prefix = "fd00:cf:21:1::2/64",
    .gateways = "fd00:cf:21:1::1",
    .dnses = "2001:4860:4860::8888",
};
const DataService::IpParams kNoIpv6 = {};

// Output of +CGCONTRDP and +CGDCONT? before IPv6 support, for any PDP type.
constexpr char kIpv4DynamicParamLine[] =
    "+CGCONTRDP: 1,5,\"ctlte\",192.168.97.2/30,192.168.97.1,8.8.8.8";

std::vector<std::string> DynamicParams(const std::string& pdp_type,
                                       const DataService::IpParams& ipv6) {
  return DataService::DynamicParamLines(
      DataService::MakePDPContext(1, pdp_type, "\"ctlte\"", kIpv4, ipv6));
}

std::string PDPContextLine(const std::string& pdp_type,
                           const DataService::IpParams& ipv6) {
  return DataService::PDPContextLine(
      DataService::MakePDPContext(1, pdp_type, "\"ctlte\"", kIpv4, ipv6));
}

TEST(DataServiceDynamicParams, IpWithoutIpv6) {
  EXPECT_THAT(DynamicParams("\"IP\"", kNoIpv6),
              ElementsAre(kIpv4DynamicParamLine));
  EXPECT_EQ(PDPContextLine("\"IP\"", kNoIpv6),
            "+CGDCONT: 1,\"IP\",\"ctlte\",192.168.97.2/30,0,0");
}

TEST(DataServiceDynamicParams, IpWithIpv6) {
  EXPECT_THAT(DynamicParams("\"IP\"", kIpv6),
              ElementsAre(kIpv4DynamicParamLine,
                          "+CGCONTRDP: 1,5,\"ctlte\",fd00:cf:21:1::2/64,"
                          "fd00:cf:21:1::1,2001:4860:4860::8888"));
  EXPECT_EQ(PDPContextLine("\"IP\"", kIpv6),
            "+CGDCONT: 1,\"IP\",\"ctlte\",192.168.97.2/30,0,0");
}

TEST(DataServiceDynamicParams, Ipv4v6WithoutIpv6) {
  EXPECT_THAT(DynamicParams("\"IPV4V6\"", kNoIpv6),
              ElementsAre(kIpv4DynamicParamLine));
  EXPECT_EQ(PDPContextLine("\"IPV4V6\"", kNoIpv6),
            "+CGDCONT: 1,\"IPV4V6\",\"ctlte\",192.168.97.2/30,0,0");
}

TEST(DataServiceDynamicParams, Ipv4v6WithIpv6) {
  EXPECT_THAT(DynamicParams("\"IPV4V6\"", kIpv6),
              ElementsAre(kIpv4DynamicParamLine,
                          "+CGCONTRDP: 1,5,\"ctlte\",fd00:cf:21:1::2/64,"
                          "fd00:cf:21:1::1,2001:4860:4860::8888"));
  // +CGDCONT? keeps reporting the IPv4 address, as before.
  EXPECT_EQ(PDPContextLine("\"IPV4V6\"", kIpv6),
            "+CGDCONT: 1,\"IPV4V6\",\"ctlte\",192.168.97.2/30,0,0");
}

TEST(DataServiceDynamicParams, Ipv6WithoutIpv6) {
  EXPECT_THAT(DynamicParams("\"IPV6\"", kNoIpv6),
              ElementsAre(kIpv4DynamicParamLine));
  EXPECT_EQ(PDPContextLine("\"IPV6\"", kNoIpv6),
            "+CGDCONT: 1,\"IPV6\",\"ctlte\",192.168.97.2/30,0,0");
}

TEST(DataServiceDynamicParams, Ipv6WithIpv6) {
  EXPECT_THAT(DynamicParams("\"IPV6\"", kIpv6),
              ElementsAre(kIpv4DynamicParamLine,
                          "+CGCONTRDP: 1,5,\"ctlte\",fd00:cf:21:1::2/64,"
                          "fd00:cf:21:1::1,2001:4860:4860::8888"));
  EXPECT_EQ(PDPContextLine("\"IPV6\"", kIpv6),
            "+CGDCONT: 1,\"IPV6\",\"ctlte\",192.168.97.2/30,0,0");
}

TEST(DataServiceDynamicParams, UnquotedPdpType) {
  EXPECT_THAT(DynamicParams("IPV4V6", kIpv6),
              ElementsAre(kIpv4DynamicParamLine,
                          "+CGCONTRDP: 1,5,\"ctlte\",fd00:cf:21:1::2/64,"
                          "fd00:cf:21:1::1,2001:4860:4860::8888"));
}

}  // namespace
}  // namespace cuttlefish
