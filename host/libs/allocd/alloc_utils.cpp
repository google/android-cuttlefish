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
#include "host/libs/allocd/alloc_utils.h"

#include <cstdint>
#include <fstream>

#include "android-base/logging.h"

namespace cuttlefish {

int RunExternalCommand(const std::string& command) {
  FILE* fp;
  LOG(INFO) << "Running external command: " << command;
  fp = popen(command.c_str(), "r");

  if (fp == nullptr) {
    LOG(WARNING) << "Error running external command";
    return -1;
  }

  int status = pclose(fp);
  int ret = -1;
  if (status == -1) {
    LOG(WARNING) << "pclose error";
  } else {
    if (WIFEXITED(status)) {
      LOG(INFO) << "child process exited normally";
      ret = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      LOG(WARNING) << "child process was terminated by a signal";
    } else {
      LOG(WARNING) << "child process did not terminate normally";
    }
  }
  return ret;
}

bool AddTapIface(const std::string& name) {
  std::stringstream ss;
  ss << "ip tuntap add dev " << name << " mode tap group cvdnetwork vnet_hdr";
  auto add_command = ss.str();
  LOG(INFO) << "Create tap interface: " << add_command;
  int status = RunExternalCommand(add_command);
  return status == 0;
}

bool ShutdownIface(const std::string& name) {
  std::stringstream ss;
  ss << "ip link set dev " << name << " down";
  auto link_command = ss.str();
  LOG(INFO) << "Shutdown tap interface: " << link_command;
  int status = RunExternalCommand(link_command);

  return status == 0;
}

bool BringUpIface(const std::string& name) {
  std::stringstream ss;
  ss << "ip link set dev " << name << " up";
  auto link_command = ss.str();
  LOG(INFO) << "Bring up tap interface: " << link_command;
  int status = RunExternalCommand(link_command);

  return status == 0;
}

bool CreateEthernetIface(const std::string& name, const std::string& bridge_name,
                         bool has_ipv4_bridge, bool has_ipv6_bridge,
                         bool use_ebtables_legacy) {
  // assume bridge exists

  EthernetNetworkConfig config{false, false, false};

  if (!CreateTap(name)) {
    return false;
  }

  config.has_tap = true;

  if (!LinkTapToBridge(name, bridge_name)) {
    CleanupEthernetIface(name, config);
    return false;
  }

  if (!has_ipv4_bridge) {
    if (!CreateEbtables(name, true, use_ebtables_legacy)) {
      CleanupEthernetIface(name, config);
      return false;
    }
    config.has_broute_ipv4 = true;
  }

  if (!has_ipv6_bridge) {
    if (CreateEbtables(name, false, use_ebtables_legacy)) {
      CleanupEthernetIface(name, config);
      return false;
    }
    config.has_broute_ipv6 = true;
  }

  return true;
}

std::string MobileGatewayName(const std::string& ipaddr, uint16_t id) {
  std::stringstream ss;
  ss << ipaddr << "." << (4 * id + 1);
  return ss.str();
}

std::string MobileNetworkName(const std::string& ipaddr,
                              const std::string& netmask, uint16_t id) {
  std::stringstream ss;
  ss << ipaddr << "." << (4 * id) << netmask;
  return ss.str();
}

bool CreateMobileIface(const std::string& name, uint16_t id,
                       const std::string& ipaddr) {
  if (id > kMaxIfaceNameId) {
    LOG(ERROR) << "ID exceeds maximum value to assign a netmask: " << id;
    return false;
  }

  auto netmask = "/30";
  auto gateway = MobileGatewayName(ipaddr, id);
  auto network = MobileNetworkName(ipaddr, netmask, id);

  if (!CreateTap(name)) {
    return false;
  }

  if (!AddGateway(name, gateway, netmask)) {
    DestroyIface(name);
  }

  if (!IptableConfig(network, true)) {
    DestroyGateway(name, gateway, netmask);
    DestroyIface(name);
    return false;
  };

  return true;
}

bool DestroyMobileIface(const std::string& name, uint16_t id,
                        const std::string& ipaddr) {
  if (id > 63) {
    LOG(ERROR) << "ID exceeds maximum value to assign a netmask: " << id;
    return false;
  }

  auto netmask = "/30";
  auto gateway = MobileGatewayName(ipaddr, id);
  auto network = MobileNetworkName(ipaddr, netmask, id);

  IptableConfig(network, false);
  DestroyGateway(name, gateway, netmask);
  return DestroyIface(name);
}

bool AddGateway(const std::string& name, const std::string& gateway,
                const std::string& netmask) {
  std::stringstream ss;
  ss << "ip addr add " << gateway << netmask << " broadcast + dev " << name;
  auto command = ss.str();
  LOG(INFO) << "setup gateway: " << command;
  int status = RunExternalCommand(command);

  return status == 0;
}

bool DestroyGateway(const std::string& name, const std::string& gateway,
                    const std::string& netmask) {
  std::stringstream ss;
  ss << "ip addr del " << gateway << netmask << " broadcast + dev " << name;
  auto command = ss.str();
  LOG(INFO) << "removing gateway: " << command;
  int status = RunExternalCommand(command);

  return status == 0;
}

bool DestroyEthernetIface(const std::string& name, bool has_ipv4_bridge,
                          bool has_ipv6_bridge, bool use_ebtables_legacy) {
  if (!has_ipv6_bridge) {
    DestroyEbtables(name, false, use_ebtables_legacy);
  }

  if (!has_ipv4_bridge) {
    DestroyEbtables(name, true, use_ebtables_legacy);
  }

  return DestroyIface(name);
}

void CleanupEthernetIface(const std::string& name,
                          const EthernetNetworkConfig& config) {
  if (config.has_broute_ipv6) {
    DestroyEbtables(name, false, config.use_ebtables_legacy);
  }

  if (config.has_broute_ipv4) {
    DestroyEbtables(name, true, config.use_ebtables_legacy);
  }

  if (config.has_tap) {
    DestroyIface(name);
  }
}

bool CreateEbtables(const std::string& name, bool use_ipv4,
                    bool use_ebtables_legacy) {
  return EbtablesBroute(name, use_ipv4, true, use_ebtables_legacy) &&
         EbtablesFilter(name, use_ipv4, true, use_ebtables_legacy);
}

bool DestroyEbtables(const std::string& name, bool use_ipv4,
                     bool use_ebtables_legacy) {
  return EbtablesBroute(name, use_ipv4, false, use_ebtables_legacy) &&
         EbtablesFilter(name, use_ipv4, false, use_ebtables_legacy);
}

bool EbtablesBroute(const std::string& name, bool use_ipv4, bool add,
                    bool use_ebtables_legacy) {
  std::stringstream ss;
  // we don't know the name of the ebtables program, but since we're going to
  // exec this program name, make sure they can only choose between the two
  // options we currently support, and not something they can overwrite
  if (use_ebtables_legacy) {
    ss << kEbtablesLegacyName;
  } else {
    ss << kEbtablesName;
  }

  ss << " -t broute " << (add ? "-A" : "-D") << " BROUTING -p "
     << (use_ipv4 ? "ipv4" : "ipv6") << " --in-if " << name << " -j DROP";
  auto command = ss.str();
  int status = RunExternalCommand(command);

  return status == 0;
}

bool EbtablesFilter(const std::string& name, bool use_ipv4, bool add,
                    bool use_ebtables_legacy) {
  std::stringstream ss;
  if (use_ebtables_legacy) {
    ss << kEbtablesLegacyName;
  } else {
    ss << kEbtablesName;
  }

  ss << " -t filter " << (add ? "-A" : "-D") << " FORWARD -p "
     << (use_ipv4 ? "ipv4" : "ipv6") << " --out-if " << name << " -j DROP";
  auto command = ss.str();
  int status = RunExternalCommand(command);

  return status == 0;
}

bool LinkTapToBridge(const std::string& tap_name,
                     const std::string& bridge_name) {
  std::stringstream ss;
  ss << "ip link set dev " << tap_name << " master " << bridge_name;
  auto command = ss.str();
  int status = RunExternalCommand(command);

  return status == 0;
}

bool CreateTap(const std::string& name) {
  LOG(INFO) << "Attempt to create tap interface: " << name;
  if (!AddTapIface(name)) {
    LOG(WARNING) << "Failed to create tap interface: " << name;
    return false;
  }

  if (!BringUpIface(name)) {
    LOG(WARNING) << "Failed to bring up tap interface: " << name;
    DeleteIface(name);
    return false;
  }

  return true;
}

bool DeleteIface(const std::string& name) {
  std::stringstream ss;
  ss << "ip link delete " << name;
  auto link_command = ss.str();
  LOG(INFO) << "Delete tap interface: " << link_command;
  int status = RunExternalCommand(link_command);

  return status == 0;
}

bool DestroyIface(const std::string& name) {
  if (!ShutdownIface(name)) {
    LOG(WARNING) << "Failed to shutdown tap interface: " << name;
    // the interface might have already shutdown ... so ignore and try to remove
    // the interface. In the future we could read from the pipe and handle this
    // case more elegantly
  }

  if (!DeleteIface(name)) {
    LOG(WARNING) << "Failed to delete tap interface: " << name;
    return false;
  }

  return true;
}

std::optional<std::string> GetUserName(uid_t uid) {
  passwd* pw = getpwuid(uid);
  if (pw) {
    std::string ret(pw->pw_name);
    return ret;
  }
  return std::nullopt;
}

bool CreateBridge(const std::string& name) {
  std::stringstream ss;
  ss << "ip link add name " << name
     << " type bridge forward_delay 0 stp_state 0";

  auto command = ss.str();
  LOG(INFO) << "create bridge: " << command;
  int status = RunExternalCommand(command);

  if (status != 0) {
    return false;
  }

  return BringUpIface(name);
}

bool DestroyBridge(const std::string& name) { return DeleteIface(name); }

bool SetupBridgeGateway(const std::string& bridge_name,
                        const std::string& ipaddr) {
  GatewayConfig config{false, false, false};
  auto gateway = ipaddr + ".1";
  auto netmask = "/24";
  auto network = ipaddr + ".0" + netmask;
  auto dhcp_range = ipaddr + ".2," + ipaddr + ".255";

  if (!AddGateway(bridge_name, gateway, netmask)) {
    return false;
  }

  config.has_gateway = true;

  if (StartDnsmasq(bridge_name, gateway, dhcp_range)) {
    CleanupBridgeGateway(bridge_name, ipaddr, config);
    return false;
  }

  config.has_dnsmasq = true;

  auto ret = IptableConfig(network, true);
  if (!ret) {
    CleanupBridgeGateway(bridge_name, ipaddr, config);
    LOG(WARNING) << "Failed to setup ip tables";
  }

  return ret;
}

void CleanupBridgeGateway(const std::string& name, const std::string& ipaddr,
                          const GatewayConfig& config) {
  auto gateway = ipaddr + ".1";
  auto netmask = "/24";
  auto network = ipaddr + ".0" + netmask;
  auto dhcp_range = ipaddr + ".2," + ipaddr + ".255";

  if (config.has_iptable) {
    IptableConfig(network, false);
  }

  if (config.has_dnsmasq) {
    StopDnsmasq(name);
  }

  if (config.has_gateway) {
    DestroyGateway(name, gateway, netmask);
  }
}

bool StartDnsmasq(const std::string& bridge_name, const std::string& gateway,
                  const std::string& dhcp_range) {
  auto dns_servers = "8.8.8.8,8.8.4.4";
  auto dns6_servers = "2001:4860:4860::8888,2001:4860:4860::8844";
  std::stringstream ss;

  // clang-format off
  ss << 
  "dnsmasq"
    " --port=0"
    " --strict-order"
    " --except-interface=lo"
    " --interface=" << bridge_name << 
    " --listen-address=" << gateway << 
    " --bind-interfaces"
    " --dhcp-range=" << dhcp_range << 
    " --dhcp-option=\"option:dns-server," << dns_servers << "\""
    " --dhcp-option=\"option6:dns-server," << dns6_servers << "\""
    " --conf-file=\"\""
    " --pid-file=/var/run/cuttlefish-dnsmasq-" << bridge_name << ".pid"
    " --dhcp-leasefile=/var/run/cuttlefish-dnsmasq-" << bridge_name << ".leases"
    " --dhcp-no-override ";
  // clang-format on

  auto command = ss.str();
  LOG(INFO) << "start_dnsmasq: " << command;
  int status = RunExternalCommand(command);

  return status == 0;
}

bool StopDnsmasq(const std::string& name) {
  std::ifstream file;
  std::string filename = "/var/run/cuttlefish-dnsmasq-" + name + ".pid";
  LOG(INFO) << "stopping dsnmasq for interface: " << name;
  file.open(filename);
  if (file.is_open()) {
    LOG(INFO) << "dnsmasq file:" << filename
              << " could not be opened, assume dnsmaq has already stopped";
    return true;
  }

  std::string pid;
  file >> pid;
  file.close();
  std::string command = "kill " + pid;
  int status = RunExternalCommand(command);
  auto ret = (status == 0);

  if (ret) {
    LOG(INFO) << "dsnmasq for:" << name << "successfully stopped";
  } else {
    LOG(WARNING) << "Failed to stop dsnmasq for:" << name;
  }
  return ret;
}

bool IptableConfig(const std::string& network, bool add) {
  std::stringstream ss;
  ss << "iptables -t nat " << (add ? "-A" : "-D") << " POSTROUTING -s "
     << network << " -j MASQUERADE";

  auto command = ss.str();
  LOG(INFO) << "iptable_config: " << command;
  int status = RunExternalCommand(command);

  return status == 0;
}

bool CreateEthernetBridgeIface(const std::string& name,
                               const std::string& ipaddr) {
  if (!CreateBridge(name)) {
    return false;
  }

  if (!SetupBridgeGateway(name, ipaddr)) {
    DestroyBridge(name);
    return false;
  }

  return true;
}

bool DestroyEthernetBridgeIface(const std::string& name,
                                const std::string& ipaddr) {
  GatewayConfig config{true, true, true};

  // Don't need to check if removing some part of the config failed, we need to
  // remove the entire interface, so just ignore any error until the end
  CleanupBridgeGateway(name, ipaddr, config);

  return DestroyBridge(name);
}

}  // namespace cuttlefish
