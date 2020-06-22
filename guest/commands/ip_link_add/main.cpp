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

#include <linux/rtnetlink.h>
#include <net/if.h>
#include <iostream>
#include <string>

int main(int argc, char *argv[]) {
  if (!((argc == 5 && std::string(argv[1]) == "vlan") ||
        (argc == 4 && std::string(argv[1]) == "virt_wifi"))) {
    std::cerr << "usages:\n";
    std::cerr << "  " << argv[0] << " vlan [ethA] [ethB] [index]\n";
    std::cerr << "  " << argv[0] << " virt_wifi [ethA] [ethB]\n";
    return -1;
  }
  const char *const name = argv[2];
  int32_t index = if_nametoindex(name);
  if (index == 0) {
    fprintf(stderr, "%s: invalid interface name '%s'\n", argv[2], name);
    return -2;
  }
  const char *const new_name = argv[3];
  auto factory = cuttlefish::NetlinkClientFactory::Default();
  std::unique_ptr<cuttlefish::NetlinkClient> nl(factory->New(NETLINK_ROUTE));

  // http://maz-programmersdiary.blogspot.com/2011/09/netlink-sockets.html
  cuttlefish::NetlinkRequest link_add_request(RTM_NEWLINK, NLM_F_REQUEST|NLM_F_ACK|0x600);
  link_add_request.Append(ifinfomsg {
    .ifi_change = 0xFFFFFFFF,
  });
  link_add_request.AddString(IFLA_IFNAME, std::string(new_name));
  link_add_request.AddInt(IFLA_LINK, index);

  link_add_request.PushList(IFLA_LINKINFO);
  link_add_request.AddString(IFLA_INFO_KIND, argv[1]);
  link_add_request.PushList(IFLA_INFO_DATA);
  if (std::string(argv[1]) == "vlan") {
    uint16_t vlan_index = atoi(argv[4]);
    link_add_request.AddInt(IFLA_VLAN_ID, vlan_index);
  }
  link_add_request.PopList();
  link_add_request.PopList();

  nl->Send(link_add_request);

  cuttlefish::NetlinkRequest bring_up_backing_request(RTM_SETLINK, NLM_F_REQUEST|NLM_F_ACK|0x600);
  bring_up_backing_request.Append(ifinfomsg {
    .ifi_index = index,
    .ifi_flags = IFF_UP,
    .ifi_change = 0xFFFFFFFF,
  });

  nl->Send(bring_up_backing_request);

  return 0;
}
