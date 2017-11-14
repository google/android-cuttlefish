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
#include "host/commands/wifid/netlink.h"

#include <glog/logging.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>

namespace avd {
namespace {
constexpr char kWifiSimFamilyName[] = "MAC80211_HWSIM";
constexpr char kNl80211FamilyName[] = "nl80211";
}  // namespace

Netlink::Netlink() : genl_(NETLINK_GENERIC), rtnl_(NETLINK_ROUTE) {}

bool Netlink::Init() {
  if (!genl_.Init()) {
    LOG(ERROR) << "Could not open Netlink Generic.";
    return false;
  }

  if (!rtnl_.Init()) {
    LOG(ERROR) << "Could not open Netlink Route.";
    return false;
  }

  // Start the thread processing asynchronous netlink responses.
  netlink_thread_.reset(new std::thread([this]() { HandleNetlinkMessages(); }));

  // Query relevant netlink families:
  // MAC80211 family allows us to create virtual radios and corresponding
  // interfaces.
  mac80211_hwsim_family_ = genl_ctrl_resolve(genl_.Sock(), kWifiSimFamilyName);
  if (mac80211_hwsim_family_ < 0) {
    LOG(ERROR) << "Could not find virtual wifi family. Please make sure module "
               << "'mac80211_hwsim' is loaded on your system.";
    return false;
  }
  LOG(INFO) << "MAC80211_HWSIM found with family id: "
            << mac80211_hwsim_family_;

  // NL80211 family allows us to find radios and corresponding interfaces.
  nl80211_family_ = genl_ctrl_resolve(genl_.Sock(), kNl80211FamilyName);
  if (nl80211_family_ < 0) {
    LOG(ERROR) << "Could not find nl80211 family. WIFI stack is unavailable.";
    return false;
  }
  LOG(INFO) << "NL80211 found with family id: " << nl80211_family_;

  return true;
}

void Netlink::HandleNetlinkMessages() {
  fd_set nlfds;
  int genl_fd = nl_socket_get_fd(GeNL().Sock());
  int rtnl_fd = nl_socket_get_fd(RtNL().Sock());
  int max_fd = std::max(genl_fd, rtnl_fd) + 1;

  while (true) {
    FD_ZERO(&nlfds);
    FD_SET(genl_fd, &nlfds);
    FD_SET(rtnl_fd, &nlfds);

    int res = select(max_fd, &nlfds, nullptr, nullptr, nullptr);
    if (res <= 0) continue;

    if (FD_ISSET(genl_fd, &nlfds)) nl_recvmsgs_default(GeNL().Sock());
    if (FD_ISSET(rtnl_fd, &nlfds)) nl_recvmsgs_default(RtNL().Sock());
  }
}

}  // namespace avd
