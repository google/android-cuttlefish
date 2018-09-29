/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/libs/net/netlink_client.h"
#include "common/libs/net/netlink_request.h"
#include "common/libs/net/network_interface.h"
#include "common/libs/net/network_interface_manager.h"
#include "common/libs/glog/logging.h"

#include <linux/rtnetlink.h>
#include <net/if.h>
#include <iostream>
#include <string>
#include <cstdlib>

int IpLinkAdd(const std::string& source, const std::string& destination,
                 const std::string& type, int vlan_index) {
  auto factory = cvd::NetlinkClientFactory::Default();
  std::unique_ptr<cvd::NetlinkClient> nl(factory->New(NETLINK_ROUTE));

  // http://maz-programmersdiary.blogspot.com/2011/09/netlink-sockets.html
  cvd::NetlinkRequest link_add_request(RTM_NEWLINK,
                                       NLM_F_REQUEST|NLM_F_ACK|0x600);
  link_add_request.Append(ifinfomsg {
    .ifi_change = 0xFFFFFFFF,
  });
  int32_t index = if_nametoindex(source.c_str());
  if (index == 0) {
    LOG(ERROR) << "setup_network: invalid interface name '" << source << "'\n";
    return -2;
  }
  link_add_request.AddString(IFLA_IFNAME, destination);
  link_add_request.AddInt(IFLA_LINK, index);

  link_add_request.PushList(IFLA_LINKINFO);
  link_add_request.AddString(IFLA_INFO_KIND, type.c_str());
  link_add_request.PushList(IFLA_INFO_DATA);
  if (type == "vlan") {
    link_add_request.AddInt(IFLA_VLAN_ID, vlan_index);
  }
  link_add_request.PopList();
  link_add_request.PopList();

  bool link_add_success = nl->Send(link_add_request);
  if (!link_add_success) {
    LOG(ERROR) << "setup_network: could not add link " << destination;
    return -3;
  }

  cvd::NetlinkRequest bring_up_backing_request(RTM_SETLINK,
                                               NLM_F_REQUEST|NLM_F_ACK|0x600);
  bring_up_backing_request.Append(ifinfomsg {
    .ifi_index = index,
    .ifi_flags = IFF_UP,
    .ifi_change = 0xFFFFFFFF,
  });

  bool link_backing_up = nl->Send(bring_up_backing_request);
  if (!link_backing_up) {
    LOG(ERROR) << "setup_network: could not bring up backing " << source;
    return -4;
  }

  return 0;
}

int CreateVlan(const std::string& source, const std::string& destination,
               int vlan_index) {
  return IpLinkAdd(source, destination, "vlan", vlan_index);
}

int CreateWifiWrapper(const std::string& source,
                      const std::string& destination) {
  return IpLinkAdd(source, destination, "virt_wifi", -1);
}

int RenameNetwork(const std::string& name, const std::string& new_name) {
  static auto net_manager =
      cvd::NetworkInterfaceManager::New(cvd::NetlinkClientFactory::Default());
  auto connection = net_manager->Open(name, "ignore");
  if (!connection) {
    LOG(ERROR) << "setup_network: could not open " << name << " on device.";
    return -1;
  }
  connection->SetName(new_name);
  bool changes_applied = net_manager->ApplyChanges(*connection);
  if (!changes_applied) {
    LOG(ERROR) << "setup_network: can't rename " << name << " to " << new_name;
    return -1;
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc != 2) {
    LOG(ERROR) << "setup_network: must be invoked with only "
               << "${ro.boot.cuttlefish_network}";
  }
  int ret = 0;
  if (std::string(argv[1]) == "vlan") {
    ret += RenameNetwork("eth0", "buried_eth0");
    ret += CreateVlan("buried_eth0", "buried_wlan0", 11);
    ret += CreateWifiWrapper("buried_wlan0", "wlan0");
    ret += CreateVlan("buried_eth0", "rmnet0", 12);
  } else if (std::string(argv[1]) == "legacy") {
    ret += RenameNetwork("eth0", "buried_eth0");
    ret += CreateWifiWrapper("buried_eth0", "wlan0");
    ret += RenameNetwork("eth1", "rmnet0");
  } else {
    LOG(ERROR) << "setup_network: unknown configuration '" << argv[1] << "'";
  }
  return ret;
}
