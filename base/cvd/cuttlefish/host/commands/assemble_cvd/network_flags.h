/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include <stdint.h>

#include <optional>
#include <string>

#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// IPv6 parameters the modem simulator hands to the guest RIL for the mobile
// network.
struct MobileIpv6Config {
  std::string ipaddr;
  std::string gateway;
  uint8_t prefixlen = 0;
};

// Derives the guest's IPv6 parameters from the host's address on a routed
// mobile tap, the same way the IPv4 parameters are derived: the host address
// is the gateway and the guest gets the lowest other address in the prefix
// (prefix::2 when the host has prefix::1). Returns nullopt when the prefix has
// no room for a guest address.
std::optional<MobileIpv6Config> MobileIpv6ConfigFromHostAddress(
    const in6_addr& host_addr, const in6_addr& netmask);

Result<void> ConfigureNetworkSettings(
    const std::string& ril_dns_arg, const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& const_instance,
    CuttlefishConfig::MutableInstanceSpecific& instance);

}  // namespace cuttlefish
