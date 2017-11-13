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

#include "host/commands/wifid/virtual_wifi.h"

#include <fstream>

#include <glog/logging.h>
#include <linux/nl80211.h>
#include <netlink/genl/ctrl.h>

#include "host/commands/wifid/cmd.h"
#include "host/commands/wifid/mac80211.h"

namespace avd {
namespace {
// Create new HWSIM Radio.
// Returns newly created HWSIM radio number, or negative errno code.
int CreateHWSIM(Netlink* nl, const std::string& wiphy_name) {
  Cmd msg;

  if (!genlmsg_put(msg.Msg(), NL_AUTO_PID, NL_AUTO_SEQ, nl->FamilyMAC80211(), 0,
                   NLM_F_REQUEST, HWSIM_CMD_NEW_RADIO, kWifiSimVersion) ||
      nla_put_string(msg.Msg(), HWSIM_ATTR_RADIO_NAME, wiphy_name.c_str()) ||
      nla_put_flag(msg.Msg(), HWSIM_ATTR_DESTROY_RADIO_ON_CLOSE)) {
    LOG(ERROR) << "Could not create new radio request.";
    return -1;
  }

  nl->GeNL().Send(&msg);

  // Responses() pauses until netlink responds to previously sent message.
  for (auto* r : msg.Responses()) {
    auto hdr = nlmsg_hdr(r);
    if (hdr->nlmsg_type == NLMSG_ERROR) {
      nlmsgerr* err = static_cast<nlmsgerr*>(nlmsg_data(hdr));
      return err->error;
    }
  }

  LOG(ERROR) << "Unknown or no response from netlink.";
  return -1;
}

// Destroy existing HWSIM Radio.
int DeleteHWSIM(Netlink* nl, int hwsim_number) {
  Cmd msg;

  if (!genlmsg_put(msg.Msg(), NL_AUTO_PID, NL_AUTO_SEQ, nl->FamilyMAC80211(), 0,
                   NLM_F_REQUEST, HWSIM_CMD_DEL_RADIO, kWifiSimVersion) ||
      nla_put_u32(msg.Msg(), HWSIM_ATTR_RADIO_ID, hwsim_number)) {
    LOG(ERROR) << "Could not create del radio request.";
    return -1;
  }

  nl->GeNL().Send(&msg);

  // Responses() pauses until netlink responds to previously sent message.
  for (auto* r : msg.Responses()) {
    auto hdr = nlmsg_hdr(r);
    if (hdr->nlmsg_type == NLMSG_ERROR) {
      nlmsgerr* err = static_cast<nlmsgerr*>(nlmsg_data(hdr));
      return err->error;
    }
  }

  LOG(ERROR) << "Unknown or no response from netlink.";
  return -1;
}

// Get WIPHY index number associated with a specified name.
// Note: WIPHY number is not the same as HWSIM number:
// - the former identifies physical radio in the system,
// - the latter identifies simulated radio in the system.
// TODO(ender): we can get the interface number from sysfs, but
// the information we receive from HWSIM is not enough to get the
// wiphy # from the system directly. Update this when there is a better
// way to acquire radio number.
int GetWIPHYIndex(const std::string& wiphy_name) {
  int number;
  std::ifstream file("/sys/class/ieee80211/" + wiphy_name + "/index");
  file >> number;
  return number;
}

// Get WLAN interface index associated with specific WIPHY index.
// Returns interface index (> 0) or negative value indicating errno code.
int GetWiphyInterface(Netlink* nl, uint32_t wiphy_index) {
  Cmd msg;

  if (!genlmsg_put(msg.Msg(), NL_AUTO_PID, NL_AUTO_SEQ, nl->FamilyNL80211(), 0,
                   NLM_F_REQUEST | NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0)) {
    LOG(ERROR) << "Could not create interface query.";
    return -1;
  }

  nl->GeNL().Send(&msg);

  // Responses() pauses until netlink responds to previously sent message.
  for (auto* r : msg.Responses()) {
    auto hdr = nlmsg_hdr(r);
    if (hdr->nlmsg_type == NLMSG_ERROR) {
      nlmsgerr* err = static_cast<nlmsgerr*>(nlmsg_data(hdr));
      LOG(ERROR) << "Could not query wiphy: " << strerror(-err->error);
      return err->error;
    }

    // Last message in entire series.
    if (hdr->nlmsg_type == NLMSG_DONE) break;

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
    if (wiphy != nullptr && nla_get_u32(wiphy) == wiphy_index) {
      auto number = attrs[NL80211_ATTR_IFINDEX];

      if (number != nullptr) {
        return nla_get_u32(number);
      }
    }
  }

  LOG(INFO) << "No interfaces found for wiphy " << wiphy_index;
  return -1;
}

// Set WLAN interface name.
// Uses Netlink Route to alter interface attributes (currently: name).
bool SetWLANInterface(Netlink* nl, int iface_index, const std::string& name) {
  Cmd msg;

  ifinfomsg ifm{};
  ifm.ifi_index = iface_index;

  if (!nlmsg_put(msg.Msg(), NL_AUTO_PID, NL_AUTO_SEQ, RTM_SETLINK, 0,
                 NLM_F_REQUEST) ||
      nlmsg_append(msg.Msg(), &ifm, sizeof(ifm), 0) ||
      nla_put_string(msg.Msg(), IFLA_IFNAME, name.c_str())) {
    LOG(ERROR) << "Could not create interface update.";
    return false;
  }

  nl->RtNL().Send(&msg);

  // Responses() pauses until netlink responds to previously sent message.
  for (auto* r : msg.Responses()) {
    auto hdr = nlmsg_hdr(r);
    if (hdr->nlmsg_type == NLMSG_ERROR) {
      nlmsgerr* err = static_cast<nlmsgerr*>(nlmsg_data(hdr));
      LOG_IF(ERROR, err->error < 0) << "Failed to update iface " << iface_index
                                    << ": " << strerror(-err->error);
      return err->error == 0;
    }
  }

  LOG(ERROR) << "Unknown or no response from netlink.";
  return -1;
}
}  // namespace

VirtualWIFI::~VirtualWIFI() {
  LOG(INFO) << "Deleting virtual wifi: " << hwsim_number_;
  if (hwsim_number_ > 0) {
    auto res = DeleteHWSIM(nl_, hwsim_number_);
    LOG_IF(ERROR, res < 0) << "Could not delete radio: " << strerror(-res);
    hwsim_number_ = 0;
  }
  LOG(INFO) << "Done.";
}

bool VirtualWIFI::Init() {
  std::string phy = name_ + "phy";
  // Each WLAN device consists of two sides:
  // - WIPHY is the "radio" side,
  // - WLAN is the "interface" side.
  // Radios have more physical properties, while WLAN have more logical /
  // interface properties. Each radio can have more than one WLAN.

  // 1. Create new MAC80211 HWSIM radio.
  hwsim_number_ = CreateHWSIM(nl_, phy);
  if (hwsim_number_ <= 0) {
    LOG(ERROR) << "Could not create HWSIM: " << strerror(-hwsim_number_);
    return false;
  }

  // 2. Acquire the WIPHY radio number created with HWSIM radio.
  wiphy_number_ = GetWIPHYIndex(phy);
  if (wiphy_number_ <= 0) {
    LOG(ERROR) << "Could not create WIPHY.";
    return false;
  }

  // 3. Query interface index.
  iface_number_ = GetWiphyInterface(nl_, wiphy_number_);
  if (iface_number_ <= 0) {
    LOG(ERROR) << "Could not query interface details.";
    return false;
  }

  // 4. Apply requested interface name.
  if (!SetWLANInterface(nl_, iface_number_, name_)) {
    return false;
  }

  return true;
}

}  // namespace avd
