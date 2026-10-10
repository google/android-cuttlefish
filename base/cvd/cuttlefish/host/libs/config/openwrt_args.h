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

#include <netinet/in.h>

#include <optional>
#include <string>
#include <unordered_map>

#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

std::unordered_map<std::string, std::string> OpenwrtArgsFromConfig(
    const CuttlefishConfig::InstanceSpecific& instance);

// IPv6 settings passed to OpenWrt on the kernel command line when the host
// runs in IPv6 routed mode (ipv6_routed_prefix in
// /etc/default/cuttlefish-host-resources).
struct OpenwrtRoutedIpv6Args {
  std::string wan_ip6addr;    // P:23NN::2/64
  std::string wan_ip6gw;      // P:23NN::1
  std::string lan_ip6prefix;  // P:25NN::/64
};

// Derives the routed mode OpenWrt IPv6 settings from the host's address on
// cvd-wifiap-NN. In routed mode the host init script assigns P:23NN::1/64,
// where P is the routed /48 and NN is the instance number as two hex digits.
// Returns nullopt for any other address, which includes the private mode
// addresses (ULA, fd00:cf:23:<instance>::1), so OpenWrt keeps its default
// IPv6 configuration.
std::optional<OpenwrtRoutedIpv6Args> OpenwrtRoutedIpv6ArgsFromHostAddress(
    const in6_addr& host_addr, int prefix_length, int instance_num);

}  // namespace cuttlefish
