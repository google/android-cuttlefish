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
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <optional>
#include <string_view>

namespace cuttlefish {

// Wireless network prefix
inline constexpr char kWirelessIp[] = "192.168.96";
// Mobile network prefix
inline constexpr char kMobileIp[] = "192.168.97";
// Ethernet network prefix
inline constexpr char kEthernetIp[] = "192.168.98";
// permission bits for socket
inline constexpr int kSocketMode = 0666;

// Max ID an interface can have
// Note: Interface names only have 2 digits in addition to the username prefix
// Additionally limited by available netmask values in MobileNetworkName
// Exceeding 63 would result in an overflow when calculating the netmask
inline constexpr uint32_t kMaxIfaceNameId = 63;

// struct for managing configuration state
struct GatewayConfig {
  bool has_gateway = false;
  bool has_dnsmasq = false;
  bool has_iptable = false;
};

int RunExternalCommand(const std::string& command);
std::optional<std::string> GetUserName(uid_t uid);

bool AddTapIface(std::string_view name);
bool CreateTap(std::string_view name);

bool BringUpIface(std::string_view name);
bool ShutdownIface(std::string_view name);

bool DestroyIface(std::string_view name);
bool DeleteIface(std::string_view name);

bool CreateBridge(std::string_view name);
bool DestroyBridge(std::string_view name);

bool CreateMobileIface(std::string_view name, uint16_t id,
                       std::string_view ipaddr);
bool DestroyMobileIface(std::string_view name, uint16_t id,
                        std::string_view ipaddr);

bool CreateEthernetIface(std::string_view name, std::string_view bridge_name);
bool DestroyEthernetIface(std::string_view name);
void CleanupEthernetIface(std::string_view name);

bool IptableConfig(std::string_view network, bool add);

bool LinkTapToBridge(std::string_view tap_name,
                     std::string_view bridge_name);

bool SetupBridgeGateway(std::string_view name, std::string_view ipaddr);
void CleanupBridgeGateway(std::string_view name, std::string_view ipaddr,
                          const GatewayConfig& config);

bool CreateEthernetBridgeIface(std::string_view name,
                               std::string_view ipaddr);
bool DestroyEthernetBridgeIface(std::string_view name,
                                std::string_view ipaddr);

bool AddGateway(std::string_view name, std::string_view gateway,
                std::string_view netmask);
bool DestroyGateway(std::string_view name, std::string_view gateway,
                    std::string_view netmask);

bool StartDnsmasq(std::string_view bridge_name, std::string_view gateway,
                  std::string_view dhcp_range);
bool StopDnsmasq(std::string_view name);

}  // namespace cuttlefish
