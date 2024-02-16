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

#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <optional>
#include <sstream>

#include "common/libs/fs/shared_fd.h"
#include "request.h"

namespace cuttlefish {

constexpr char kEbtablesName[] = "ebtables";
constexpr char kEbtablesLegacyName[] = "ebtables-legacy";

// Wireless network prefix
constexpr char kWirelessIp[] = "192.168.96";
// Mobile network prefix
constexpr char kMobileIp[] = "192.168.97";
// Ethernet network prefix
constexpr char kEthernetIp[] = "192.168.98";
// permission bits for socket
constexpr int kSocketMode = 0666;

// Max ID an interface can have
// Note: Interface names only have 2 digits in addition to the username prefix
// Additionally limited by available netmask values in MobileNetworkName
// Exceeding 63 would result in an overflow when calculating the netmask
constexpr uint32_t kMaxIfaceNameId = 63;

// struct for managing configuration state
struct EthernetNetworkConfig {
  bool has_broute_ipv4 = false;
  bool has_broute_ipv6 = false;
  bool has_tap = false;
  bool use_ebtables_legacy = false;
};

// struct for managing configuration state
struct GatewayConfig {
  bool has_gateway = false;
  bool has_dnsmasq = false;
  bool has_iptable = false;
};

int RunExternalCommand(const std::string& command);
std::optional<std::string> GetUserName(uid_t uid);

bool AddTapIface(const std::string& name);
bool CreateTap(const std::string& name);

bool BringUpIface(const std::string& name);
bool ShutdownIface(const std::string& name);

bool DestroyIface(const std::string& name);
bool DeleteIface(const std::string& name);

bool CreateBridge(const std::string& name);
bool DestroyBridge(const std::string& name);

bool CreateEbtables(const std::string& name, bool use_ipv,
                    bool use_ebtables_legacy);
bool DestroyEbtables(const std::string& name, bool use_ipv4,
                     bool use_ebtables_legacy);
bool EbtablesBroute(const std::string& name, bool use_ipv4, bool add,
                    bool use_ebtables_legacy);
bool EbtablesFilter(const std::string& name, bool use_ipv4, bool add,
                    bool use_ebtables_legacy);

bool CreateMobileIface(const std::string& name, uint16_t id,
                       const std::string& ipaddr);
bool DestroyMobileIface(const std::string& name, uint16_t id,
                        const std::string& ipaddr);

bool CreateEthernetIface(const std::string& name, const std::string& bridge_name,
                         bool has_ipv4_bridge, bool has_ipv6_bridge,
                         bool use_ebtables_legacy);
bool DestroyEthernetIface(const std::string& name,
                          bool has_ipv4_bridge, bool use_ipv6,
                          bool use_ebtables_legacy);
void CleanupEthernetIface(const std::string& name,
                          const EthernetNetworkConfig& config);

bool IptableConfig(const std::string& network, bool add);

bool LinkTapToBridge(const std::string& tap_name,
                     const std::string& bridge_name);

bool SetupBridgeGateway(const std::string& name, const std::string& ipaddr);
void CleanupBridgeGateway(const std::string& name, const std::string& ipaddr,
                          const GatewayConfig& config);

bool CreateEthernetBridgeIface(const std::string& name,
                               const std::string &ipaddr);
bool DestroyEthernetBridgeIface(const std::string& name,
                                const std::string &ipaddr);

bool AddGateway(const std::string& name, const std::string& gateway,
                const std::string& netmask);
bool DestroyGateway(const std::string& name, const std::string& gateway,
                    const std::string& netmask);

bool StartDnsmasq(const std::string& bridge_name, const std::string& gateway,
                  const std::string& dhcp_range);
bool StopDnsmasq(const std::string& name);

}  // namespace cuttlefish
