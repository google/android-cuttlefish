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

#include "wifi_relay.h"

#include "common/libs/wifi_relay/mac80211_hwsim_driver.h"
#include "common/libs/wifi/nl_client.h"

#if defined(CUTTLEFISH_HOST)
#include "host/libs/config/host_config.h"
#endif

#include <linux/netdevice.h>
#include <linux/nl80211.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <fstream>

#if !defined(CUTTLEFISH_HOST)
DEFINE_string(
        iface_name, "wlan0", "Name of the wifi interface to be created.");
#endif

WifiRelay::WifiRelay(
        const Mac80211HwSim::MacAddress &localMAC,
        const Mac80211HwSim::MacAddress &remoteMAC)
    : mMac80211HwSim(new Mac80211HwSim(localMAC)) {
  init_check_ = mMac80211HwSim->initCheck();

  if (init_check_ < 0) {
    return;
  }

  init_check_ = mMac80211HwSim->addRemote(
          remoteMAC,
#if defined(CUTTLEFISH_HOST)
          vsoc::wifi::WifiExchangeView::GetInstance(vsoc::GetDomain().c_str())
#else
          vsoc::wifi::WifiExchangeView::GetInstance()
#endif
          );
}

int WifiRelay::initCheck() const {
  return init_check_;
}

void WifiRelay::run() {
  for (;;) {
    fd_set rs;
    FD_ZERO(&rs);

    FD_SET(mMac80211HwSim->socketFd(), &rs);
    int maxFd = mMac80211HwSim->socketFd();

    int res = select(maxFd + 1, &rs, nullptr, nullptr, nullptr);
    if (res <= 0) {
      continue;
    }

    if (FD_ISSET(mMac80211HwSim->socketFd(), &rs)) {
      mMac80211HwSim->handlePacket();
    }
  }
}

int WifiRelay::mac80211Family() const {
  return mMac80211HwSim->mac80211Family();
}

int WifiRelay::nl80211Family() const {
  return mMac80211HwSim->nl80211Family();
}

int createRadio(cvd::NlClient *nl, int familyMAC80211, const char *phyName) {
    cvd::Cmd msg;
    genlmsg_put(
            msg.Msg(),
            NL_AUTO_PID,
            NL_AUTO_SEQ,
            familyMAC80211,
            0,
            NLM_F_REQUEST,
            HWSIM_CMD_NEW_RADIO,
            cvd::kWifiSimVersion);

    nla_put_string(msg.Msg(), HWSIM_ATTR_RADIO_NAME, phyName);
    nla_put_flag(msg.Msg(), HWSIM_ATTR_DESTROY_RADIO_ON_CLOSE);

    nl->Send(&msg);

    // Responses() pauses until netlink responds to previously sent message.
    for (auto *r : msg.Responses()) {
        auto hdr = nlmsg_hdr(r);
        if (hdr->nlmsg_type == NLMSG_ERROR) {
            nlmsgerr* err = static_cast<nlmsgerr*>(nlmsg_data(hdr));
            return err->error;
        }
    }

    return -1;
}

int getPhyIndex(const std::string &phyName) {
    std::ifstream file("/sys/class/ieee80211/" + phyName + "/index");

    int number;
    file >> number;

    return number;
}

int getInterfaceIndex(cvd::NlClient *nl, int familyNL80211, uint32_t phyIndex) {
    cvd::Cmd msg;
    genlmsg_put(
            msg.Msg(),
            NL_AUTO_PID,
            NL_AUTO_SEQ,
            familyNL80211,
            0,
            NLM_F_REQUEST | NLM_F_DUMP,
            NL80211_CMD_GET_INTERFACE,
            0);

    nl->Send(&msg);

    // Responses() pauses until netlink responds to previously sent message.
    for (auto *r : msg.Responses()) {
        auto hdr = nlmsg_hdr(r);
        if (hdr->nlmsg_type == NLMSG_ERROR) {
            nlmsgerr* err = static_cast<nlmsgerr*>(nlmsg_data(hdr));
            return err->error;
        }

        // Last message in entire series.
        if (hdr->nlmsg_type == NLMSG_DONE) {
            break;
        }

        // !DONE && !ERROR => content.
        // Decode attributes supplied by netlink.
        // the genlmsg_parse puts each attribute in a respective slot in an array,
        // so we have to preallocate enough space.
        struct nlattr* attrs[NL80211_ATTR_MAX + 1];
        auto err = genlmsg_parse(hdr, 0, attrs, NL80211_ATTR_MAX, nullptr);

        // Return error if response could not be parsed. This is actually quite
        // serious.
        if (err < 0) {
            LOG(ERROR) << "Could not process netlink response: " << strerror(-err);
            return err;
        }

        // Check if we have WIPHY attribute in response -- and if it's the relevant
        // one.
        auto wiphy = attrs[NL80211_ATTR_WIPHY];
        if (wiphy != nullptr && nla_get_u32(wiphy) == phyIndex) {
            auto number = attrs[NL80211_ATTR_IFINDEX];

            if (number != nullptr) {
                return nla_get_u32(number);
            }
        }
    }

    return -1;
}

int updateInterface(
        cvd::NlClient *nlRoute,
        int ifaceIndex,
        const std::string &name,
        const uint8_t *mac) {
    cvd::Cmd msg;

    ifinfomsg ifm{};
    ifm.ifi_index = ifaceIndex;

    nlmsg_put(
            msg.Msg(), NL_AUTO_PID, NL_AUTO_SEQ, RTM_SETLINK, 0, NLM_F_REQUEST);

    nlmsg_append(msg.Msg(), &ifm, sizeof(ifm), 0);
    nla_put_string(msg.Msg(), IFLA_IFNAME, name.c_str());

    std::vector<uint8_t> macCopy(MAX_ADDR_LEN);
    memcpy(&macCopy[0], mac, ETH_ALEN);

    nla_put(msg.Msg(), IFLA_ADDRESS, MAX_ADDR_LEN, &macCopy[0]);

    nlRoute->Send(&msg);

    // Responses() pauses until netlink responds to previously sent message.
    for (auto *r : msg.Responses()) {
        auto hdr = nlmsg_hdr(r);
        LOG(VERBOSE) << "got response of type " << hdr->nlmsg_type;

        if (hdr->nlmsg_type == NLMSG_ERROR) {
            nlmsgerr* err = static_cast<nlmsgerr*>(nlmsg_data(hdr));

            if (err->error < 0) {
                LOG(ERROR) << "updateInterface failed w/ " << err->error
                              << " (" << strerror(-err->error) << ")";
            }

            return err->error;
        }
    }

    LOG(VERBOSE) << "No more responses";

    return -1;
}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto wifi_view = vsoc::wifi::WifiExchangeView::GetInstance(
#if defined(CUTTLEFISH_HOST)
      vsoc::GetDomain().c_str()
#endif
  );

  Mac80211HwSim::MacAddress guestMAC = wifi_view->GetGuestMACAddress();
  Mac80211HwSim::MacAddress hostMAC = wifi_view->GetHostMACAddress();

#ifdef CUTTLEFISH_HOST
  WifiRelay relay(hostMAC, guestMAC);
#else
  WifiRelay relay(guestMAC, hostMAC);
#endif
  int res = relay.initCheck();

  if (res < 0) {
    LOG(ERROR)
      << "WifiRelay::initCheck() returned error "
      << res
      << " ("
      << strerror(-res)
      << ")";

    exit(1);
  }

#if !defined(CUTTLEFISH_HOST)
  cvd::NlClient client(NETLINK_GENERIC);
  if (!client.Init()) {
      LOG(ERROR) << "Could not open Netlink Generic.";
      exit(1);
  }

  cvd::NlClient nlRoute(NETLINK_ROUTE);
  if (!nlRoute.Init()) {
      LOG(ERROR) << "Could not open Netlink Route.";
      exit(1);
  }

  std::unique_ptr<std::thread> nlThread(
          new std::thread([&client, &nlRoute]() {
              for (;;) {
                  fd_set rs;
                  FD_ZERO(&rs);

                  int fdGeneric = nl_socket_get_fd(client.Sock());
                  int fdRoute = nl_socket_get_fd(nlRoute.Sock());

                  FD_SET(fdGeneric, &rs);
                  FD_SET(fdRoute, &rs);

                  int maxFd = std::max(fdGeneric, fdRoute);

                  int res = select(maxFd + 1, &rs, nullptr, nullptr, nullptr);

                  if (res == 0) {
                      continue;
                  } else if (res < 0) {
                      continue;
                  }

                  if (FD_ISSET(fdGeneric, &rs)) {
                      nl_recvmsgs_default(client.Sock());
                  }

                  if (FD_ISSET(fdRoute, &rs)) {
                      nl_recvmsgs_default(nlRoute.Sock());
                  }
              }
          }));

  const std::string phyName = FLAGS_iface_name + "_phy";
  if (createRadio(&client, relay.mac80211Family(), phyName.c_str()) < 0) {
      LOG(ERROR) << "Could not create radio.";
      exit(1);
  }

  int phyIndex = getPhyIndex(phyName);
  CHECK_GE(phyIndex, 0);
  LOG(VERBOSE) << "Got PHY index " << phyIndex;

  int ifaceIndex = getInterfaceIndex(
          &client, relay.nl80211Family(), static_cast<uint32_t>(phyIndex));

  CHECK_GE(ifaceIndex, 0);
  LOG(VERBOSE) << "Got interface index " << ifaceIndex;

  if (updateInterface(
              &nlRoute, ifaceIndex, FLAGS_iface_name, &guestMAC[0]) < 0) {
      LOG(ERROR) << "Failed to update interface.";
      exit(1);
  }
#endif  // !defined(CUTTLEFISH_HOST)

  relay.run();

  return 0;
}
