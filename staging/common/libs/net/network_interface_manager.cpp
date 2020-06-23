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
#include "common/libs/net/network_interface_manager.h"

#include <arpa/inet.h>
#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

#include <memory>

#include "android-base/logging.h"
#include "common/libs/net/network_interface.h"

namespace cuttlefish {
namespace {
NetlinkRequest BuildLinkRequest(
    const NetworkInterface& interface) {
  NetlinkRequest request(RTM_SETLINK, 0);
  request.AddIfInfo(interface.Index(), interface.IsOperational());
  if (!interface.Name().empty()) {
    request.AddString(IFLA_IFNAME, interface.Name());
  }

  return request;
}

NetlinkRequest BuildAddrRequest(
    const NetworkInterface& interface) {
  NetlinkRequest request(RTM_NEWADDR, 0);
  request.AddAddrInfo(interface.Index(), interface.PrefixLength());
  in_addr_t address{inet_addr(interface.Address().c_str())};
  request.AddInt(IFA_LOCAL, address);
  request.AddInt(IFA_ADDRESS, address);
  request.AddInt(IFA_BROADCAST,
                 inet_addr(interface.BroadcastAddress().c_str()));

  return request;
}
}  // namespace

std::unique_ptr<NetworkInterfaceManager> NetworkInterfaceManager::New(
    NetlinkClientFactory* nl_factory) {
  std::unique_ptr<NetworkInterfaceManager> mgr;

  if (nl_factory == NULL) {
    nl_factory = NetlinkClientFactory::Default();
  }

  auto client = nl_factory->New(NETLINK_ROUTE);
  if (client) {
    mgr.reset(new NetworkInterfaceManager(std::move(client)));
  }

  return mgr;
}

NetworkInterfaceManager::NetworkInterfaceManager(
    std::unique_ptr<NetlinkClient> nl_client)
    : nl_client_(std::move(nl_client)) {}

std::unique_ptr<NetworkInterface> NetworkInterfaceManager::Open(
    const std::string& if_name, const std::string& if_name_alt) {
  std::unique_ptr<NetworkInterface> iface;
  // NOTE: do not replace this code with an IOCTL call.
  // On SELinux enabled Androids, RILD is not permitted to execute an IOCTL
  // and this call will fail.
  int32_t index = if_nametoindex(if_name.c_str());
  if (index == 0) {
    // Try the alternate name. This will be renamed to our preferred name
    // by the kernel, because we specify IFLA_IFNAME, but open by index.
    LOG(ERROR) << "Failed to get interface (" << if_name << ") index, "
               << "trying alternate.";
    index = if_nametoindex(if_name_alt.c_str());
    if (index == 0) {
      LOG(ERROR) << "Failed to get interface (" << if_name_alt << ") index.";
      return iface;
    }
  }

  iface.reset(new NetworkInterface(index));
  return iface;
}

bool NetworkInterfaceManager::ApplyChanges(const NetworkInterface& iface) {
  if (!nl_client_->Send(BuildLinkRequest(iface))) return false;
  // Terminate immediately if interface is down.
  if (!iface.IsOperational()) return true;
  return nl_client_->Send(BuildAddrRequest(iface));
}

}  // namespace cuttlefish
