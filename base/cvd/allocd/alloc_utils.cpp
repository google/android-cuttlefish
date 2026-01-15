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
#include "allocd/alloc_utils.h"

#include <stdint.h>

#include <fstream>
#include <string_view>
#include <sstream>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "allocd/alloc_driver.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

bool CreateEthernetIface(std::string_view name, std::string_view bridge_name) {
  // assume bridge exists

  if (!CreateTap(name)) {
    return false;
  }

  if (!LinkTapToBridge(name, bridge_name).ok()) {
    CleanupEthernetIface(name);
    return false;
  }

  return true;
}

std::string MobileGatewayName(std::string_view ipaddr, uint16_t id) {
  std::stringstream ss;
  ss << ipaddr << "." << (4 * id - 3);
  return ss.str();
}

std::string MobileNetworkName(std::string_view ipaddr,
                              std::string_view netmask, uint16_t id) {
  std::stringstream ss;
  ss << ipaddr << "." << (4 * id - 4) << netmask;
  return ss.str();
}

bool CreateMobileIface(std::string_view name, uint16_t id,
                       std::string_view ipaddr) {
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

  if (!AddGateway(name, gateway, netmask).ok()) {
    DestroyIface(name);
  }

  if (!IptableConfig(network, true).ok()) {
    DestroyGateway(name, gateway, netmask);
    DestroyIface(name);
    return false;
  };

  return true;
}

bool DestroyMobileIface(std::string_view name, uint16_t id,
                        std::string_view ipaddr) {
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

bool DestroyEthernetIface(std::string_view name) { return DestroyIface(name); }

void CleanupEthernetIface(std::string_view name) { DestroyIface(name); }

bool CreateTap(std::string_view name) {
  LOG(INFO) << "Attempt to create tap interface: " << name;
  if (!AddTapIface(name).ok()) {
    LOG(WARNING) << "Failed to create tap interface: " << name;
    return false;
  }

  if (!BringUpIface(name).ok()) {
    LOG(WARNING) << "Failed to bring up tap interface: " << name;
    DeleteIface(name);
    return false;
  }

  return true;
}

bool DestroyIface(std::string_view name) {
  if (!ShutdownIface(name).ok()) {
    LOG(WARNING) << "Failed to shutdown tap interface: " << name;
    // the interface might have already shutdown ... so ignore and try to remove
    // the interface. In the future we could read from the pipe and handle this
    // case more elegantly
  }

  if (!DeleteIface(name).ok()) {
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

bool DestroyBridge(std::string_view name) {
  Result<bool> r = BridgeInUse(name);
  if (!r.ok()) {
    return false;
  }

  if (*r) {
    // Bridge is in use. Don't proceed any further.
    return true;
  }

  return DeleteIface(name).ok();
}

bool SetupBridgeGateway(std::string_view bridge_name,
                        std::string_view ipaddr) {
  GatewayConfig config{false, false, false};
  auto gateway = absl::StrFormat("%s.1", ipaddr);
  auto netmask = "/24";
  auto network = absl::StrFormat("%s.0%s", ipaddr, netmask);
  auto dhcp_range = absl::StrFormat("%s.2,%s.255", ipaddr, ipaddr);

  if (!AddGateway(bridge_name, gateway, netmask).ok()) {
    return false;
  }

  config.has_gateway = true;

  if (!StartDnsmasq(bridge_name, gateway, dhcp_range)) {
    CleanupBridgeGateway(bridge_name, ipaddr, config);
    return false;
  }

  config.has_dnsmasq = true;

  auto ret = IptableConfig(network, true).ok();
  if (!ret) {
    CleanupBridgeGateway(bridge_name, ipaddr, config);
    LOG(WARNING) << "Failed to setup ip tables";
  }

  return ret;
}

void CleanupBridgeGateway(std::string_view name, std::string_view ipaddr,
                          const GatewayConfig& config) {
  auto gateway = absl::StrFormat("%s.1", ipaddr);
  auto netmask = "/24";
  auto network = absl::StrFormat("%s.0%s", ipaddr, netmask);
  auto dhcp_range = absl::StrFormat("%s.2,%s.255", ipaddr, ipaddr);

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

bool StartDnsmasq(std::string_view bridge_name, std::string_view gateway,
                  std::string_view dhcp_range) {
  auto dns_servers = "8.8.8.8,8.8.4.4";
  auto dns6_servers = "2001:4860:4860::8888,2001:4860:4860::8844";

  return Execute(
             {"dnsmasq", "--port=0", "--strict-order", "--except-interface=lo",
              absl::StrCat("--interface=", bridge_name),
              absl::StrCat("--listen-address=", gateway), "--bind-interfaces",
              absl::StrCat("--dhcp-range=", dhcp_range),
              absl::StrCat("--dhcp-option=option:dns-server,", dns_servers),
              absl::StrCat("--dhcp-option=option6:dns-server,", dns6_servers),
              "--conf-file=",
              absl::StrCat("--pid-file=", CvdDir(), "/cuttlefish-dnsmasq-",
                           bridge_name, ".pid"),
              absl::StrCat("--dhcp-leasefile=", CvdDir(),
                           "/cuttlefish-dnsmasq-", bridge_name, ".leases"),
              "--dhcp-no-override"}) == 0;
}

bool StopDnsmasq(std::string_view name) {
  std::ifstream file;
  std::string filename = absl::StrFormat(
      "/var/run/cuttlefish-dnsmasq-%s.pid", name);
  LOG(INFO) << "stopping dnsmasq for interface: " << name;
  file.open(filename);
  if (file.is_open()) {
    LOG(INFO) << "dnsmasq file:" << filename
              << " could not be opened, assume dnsmaq has already stopped";
    return true;
  }

  std::string pid;
  file >> pid;
  file.close();

  // TODO: Let's use kill(2) instead of subjecting ourselves to this.
  bool ret = Execute({"kill", pid}) == 0;
  if (ret) {
    LOG(INFO) << "dnsmasq for:" << name << "successfully stopped";
  } else {
    LOG(WARNING) << "Failed to stop dnsmasq for:" << name;
  }
  return ret;
}

bool CreateEthernetBridgeIface(std::string_view name,
                               std::string_view ipaddr) {
  auto exists = BridgeExists(name);
  if (exists.ok() && *exists) {
    LOG(INFO) << "Bridge " << name << " exists already, doing nothing.";
    return true;
  }

  if (!CreateBridge(name).ok()) {
    return false;
  }

  if (!SetupBridgeGateway(name, ipaddr)) {
    DestroyBridge(name);
    return false;
  }

  return true;
}

bool DestroyEthernetBridgeIface(std::string_view name,
                                std::string_view ipaddr) {
  GatewayConfig config{true, true, true};

  // Don't need to check if removing some part of the config failed, we need to
  // remove the entire interface, so just ignore any error until the end
  CleanupBridgeGateway(name, ipaddr, config);

  return DestroyBridge(name);
}

}  // namespace cuttlefish
