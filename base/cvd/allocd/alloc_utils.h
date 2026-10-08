/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <stdint.h>

#include <string>

#include "absl/strings/string_view.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Max ID an interface can have
// Note: Interface names only have 2 digits in addition to the username prefix
// Additionally limited by available netmask values in MobileNetworkName
// Exceeding 63 would result in an overflow when calculating the netmask
inline constexpr uint32_t kMaxIfaceNameId = 63;

// Netmask of the per-instance mobile and wifiap networks.
inline constexpr char kMobileNetmask[] = "/30";

// struct for managing configuration state
struct GatewayConfig {
  bool has_gateway = false;
  bool has_dnsmasq = false;
};

Result<std::string> DnsmasqPath();

bool CreateTap(absl::string_view name);

#ifdef __linux__
Result<void> ValidateTapInterfaceIsUsable(const std::string& interface_name);
#endif

bool DestroyIface(absl::string_view name);

bool DestroyBridge(absl::string_view name);

std::string MobileNetworkName(absl::string_view ipaddr,
                              absl::string_view netmask, uint16_t id);

bool CreateMobileIface(absl::string_view name, uint16_t id,
                       absl::string_view ipaddr);
bool DestroyMobileIface(absl::string_view name, uint16_t id,
                        absl::string_view ipaddr);

bool CreateEthernetIface(absl::string_view name, absl::string_view bridge_name);
bool DestroyEthernetIface(absl::string_view name);
void CleanupEthernetIface(absl::string_view name);

bool SetupBridgeGateway(absl::string_view name, absl::string_view ipaddr);
void CleanupBridgeGateway(absl::string_view name, absl::string_view ipaddr,
                          const GatewayConfig& config);

bool CreateEthernetBridgeIface(absl::string_view name,
                               absl::string_view ipaddr);
bool DestroyEthernetBridgeIface(absl::string_view name,
                                absl::string_view ipaddr);

bool StartDnsmasq(absl::string_view bridge_name, absl::string_view gateway,
                  absl::string_view dhcp_range);
bool StopDnsmasq(absl::string_view name);

}  // namespace cuttlefish
