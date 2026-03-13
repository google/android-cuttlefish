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
#include "allocd/alloc_driver.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/if_tun.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstdint>
#include <fstream>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"

#include "allocd/net/netlink_client.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

Result<unsigned int> Index(std::string_view ifname) {
  unsigned int index = if_nametoindex(std::string(ifname).c_str());
  CF_EXPECT(index != 0, "Index: " << ifname);
  return index;
}

int Prefix(std::string_view textual_netmask) {
  // Currently, the netmask argument is provided as, e.g., "/24".
  // TODO: Consider passing the number of prefix bits numerically, which
  // would require API changes across all drivers.
  std::string_view netmask = textual_netmask;
  CHECK(absl::ConsumePrefix(&netmask, "/"));

  int prefix;
  CHECK(absl::SimpleAtoi(netmask, &prefix))
      << "Prefix: couldn't get prefix from netmask: " << textual_netmask;
  return prefix;
}

}  // namespace

Result<void> AddTapIface(std::string_view name) {
  SharedFD tunfd = SharedFD::Open("/dev/net/tun", O_RDWR | O_CLOEXEC);
  CF_EXPECT(tunfd->IsOpen(), "AddTapIface: open: " << tunfd->StrError());

  struct ifreq ifr;
  strncpy(ifr.ifr_name, std::string(name).c_str(), IFNAMSIZ);
  ifr.ifr_name[IFNAMSIZ - 1] = '\0';
  ifr.ifr_flags = IFF_TAP | IFF_VNET_HDR;
  ifr.ifr_flags |= IFF_TUN_EXCL;
  int r = tunfd->Ioctl(TUNSETIFF, (void*)&ifr);
  CF_EXPECT(r != -1, "AddTapIface: TUNSETIFF: " << tunfd->StrError());

  struct group* g = getgrnam(kCvdNetworkGroupName);
  CF_EXPECT(g != NULL, "AddTapIface: getgrnam: " << tunfd->StrError());

  r = tunfd->Ioctl(TUNSETGROUP, (void*)(intptr_t)g->gr_gid);
  CF_EXPECT(r != -1, "AddTapIface: TUNSETGROUP: " << tunfd->StrError());

  r = tunfd->Ioctl(TUNSETPERSIST, (void*)1);
  CF_EXPECT(r != -1, "AddTapIface: TUNSETPERSIST: " << tunfd->StrError());

  tunfd->Close();
  return {};
}

Result<void> ShutdownIface(std::string_view name) {
  VLOG(0) << "ShutdownIface: " << name;
  auto factory = NetlinkClientFactory::Default();
  std::unique_ptr<NetlinkClient> nl = factory->New(NETLINK_ROUTE);

  NetlinkRequest req(RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK);
  req.AddIfInfo(0, false);
  req.AddString(IFLA_IFNAME, std::string(name));
  CF_EXPECT(nl->Send(req), "ShutdownIface");
  return {};
}

Result<void> BringUpIface(std::string_view name) {
  VLOG(0) << "BringUpIface: " << name;
  auto factory = NetlinkClientFactory::Default();
  std::unique_ptr<NetlinkClient> nl = factory->New(NETLINK_ROUTE);

  NetlinkRequest req(RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK);
  req.AddIfInfo(0, true);
  req.AddString(IFLA_IFNAME, std::string(name));
  CF_EXPECT(nl->Send(req), "BringUpIface");
  return {};
}

Result<void> AddGateway(std::string_view name, std::string_view gateway,
                        std::string_view netmask) {
  VLOG(0) << "AddGateway: " << name << ", " << gateway << netmask;
  auto index = Index(name);
  PCHECK(index.ok()) << "Index: " << name;

  auto factory = NetlinkClientFactory::Default();
  std::unique_ptr<NetlinkClient> nl = factory->New(NETLINK_ROUTE);

  NetlinkRequest req(RTM_NEWADDR, NLM_F_REQUEST | NLM_F_ACK);
  req.AddAddrInfo(*index, Prefix(netmask));
  in_addr_t inaddr = inet_addr(std::string(gateway).c_str());
  req.AddInAddr(IFA_LOCAL, &inaddr);
  req.AddInAddr(IFA_ADDRESS, &inaddr);

  CF_EXPECT(nl->Send(req), "AddGateway");
  return {};
}

Result<void> DestroyGateway(std::string_view name, std::string_view gateway,
                            std::string_view netmask) {
  VLOG(0) << "DestroyGateway: " << name << ", " << gateway << netmask;
  auto index = Index(name);
  if (!index.ok()) {
    return {};  // Nothing to delete.
  }

  auto factory = NetlinkClientFactory::Default();
  std::unique_ptr<NetlinkClient> nl = factory->New(NETLINK_ROUTE);

  NetlinkRequest req(RTM_DELADDR, NLM_F_REQUEST | NLM_F_ACK);
  req.AddAddrInfo(*index, Prefix(netmask));
  in_addr_t inaddr = inet_addr(std::string(gateway).c_str());
  req.AddInAddr(IFA_LOCAL, &inaddr);
  req.AddInAddr(IFA_ADDRESS, &inaddr);

  CF_EXPECT(nl->Send(req), "DestroyGateway");
  return {};
}

Result<void> LinkTapToBridge(std::string_view tap_name,
                             std::string_view bridge_name) {
  VLOG(0) << "LinkTapToBridge: " << tap_name << ", " << bridge_name;
  auto tap_index = Index(tap_name);
  PCHECK(tap_index.ok()) << "Index: " << tap_name;
  auto bridge_index = Index(bridge_name);
  PCHECK(bridge_index.ok()) << "Index: " << bridge_name;

  auto factory = NetlinkClientFactory::Default();
  std::unique_ptr<NetlinkClient> nl = factory->New(NETLINK_ROUTE);

  NetlinkRequest req(RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK);
  req.AddIfInfo(*tap_index, true);
  req.AddInt(IFLA_MASTER, *bridge_index);

  CF_EXPECT(nl->Send(req), "LinkTapToBridge");
  return {};
}

Result<void> DeleteIface(std::string_view name) {
  VLOG(0) << "DeleteIface: " << name;
  auto index = Index(name);
  if (!index.ok()) {
    return {};  // Nothing to delete.
  }

  auto factory = NetlinkClientFactory::Default();
  std::unique_ptr<NetlinkClient> nl = factory->New(NETLINK_ROUTE);

  NetlinkRequest req(RTM_DELLINK, NLM_F_REQUEST | NLM_F_ACK);
  req.AddIfInfo(*index, false);

  CF_EXPECT(nl->Send(req), "DeleteIface");
  return {};
}

Result<bool> BridgeExists(std::string_view name) {
  VLOG(0) << "BridgeExists: " << name;
  auto index = Index(name);
  if (!index.ok()) {
    CF_EXPECT(errno == ENODEV);
    return false;
  }

  return true;
}

Result<bool> BridgeInUse(std::string_view name) {
  return Execute({"sh", "-c",
                  absl::StrCat("[ $(ip link show master ", name,
                               " | wc -l) -ne 0 ]")}) == 0;
}

Result<void> CreateBridge(std::string_view name) {
  VLOG(0) << "CreateBridge: " << name;
  auto factory = NetlinkClientFactory::Default();
  std::unique_ptr<NetlinkClient> nl = factory->New(NETLINK_ROUTE);

  NetlinkRequest req(RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE);
  req.Append(ifinfomsg{
      .ifi_type = ARPHRD_NETROM,
  });
  req.AddString(IFLA_IFNAME, std::string(name));
  req.PushList(IFLA_LINKINFO);
  req.AddString(IFLA_INFO_KIND, "bridge");
  req.PushList(IFLA_INFO_DATA);
  req.AddInt(IFLA_BR_FORWARD_DELAY, 0);
  req.AddInt(IFLA_BR_STP_STATE, 0);
  req.PopList();
  req.PopList();

  CF_EXPECT(nl->Send(req), "CreateBridge");
  return {};
}

Result<void> IptableConfig(std::string_view network, bool add) {
  // TODO: Use NETLINK_NETFILTER.
  CF_EXPECT(Execute({"iptables", "-t", "nat", add ? "-A" : "-D", "POSTROUTING",
                     "-s", std::string(network), "-j", "MASQUERADE"}) == 0,
            "IptableConfig");
  return {};
}

}  // namespace cuttlefish
