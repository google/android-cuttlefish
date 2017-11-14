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

#include "host/commands/wifid/virtual_wifi_manager.h"

#include <glog/logging.h>

#include "host/commands/wifid/cmd.h"
#include "host/commands/wifid/mac80211.h"

namespace avd {
namespace {
// We don't care about byte ordering as much as we do about having all bytes
// there. Byte order does not match, we want to use it as a key in our map.
// Note: we accept const void here, because we will also process data coming
// from netlink (which is untyped).
uint64_t MACToKey(const void* macaddr) {
  auto typed = reinterpret_cast<const uint16_t*>(macaddr);
  return (1ull * typed[0] << 32) | (typed[1] << 16) | typed[2];
}
}  // namespace

// Register for asynchronous notifications from MAC80211.
// Our callback will receive data for each next frame transmitted over any
// radio.
bool VirtualWIFIManager::RegisterForSimulatorNotifications() {
  Cmd msg;

  if (!genlmsg_put(msg.Msg(), NL_AUTO_PID, NL_AUTO_SEQ, nl_->FamilyMAC80211(),
                   0, NLM_F_REQUEST, HWSIM_CMD_REGISTER, kWifiSimVersion)) {
    LOG(ERROR) << "Could not create nlmsg registration request.";
    return false;
  }

  nl_->GeNL().Send(&msg);

  for (auto* r : msg.Responses()) {
    auto hdr = nlmsg_hdr(r);
    if (hdr->nlmsg_type == NLMSG_ERROR) {
      nlmsgerr* err = static_cast<nlmsgerr*>(nlmsg_data(hdr));
      LOG_IF(ERROR, err->error < 0)
          << "Could not register for VirtualWIFIManager notifications: "
          << strerror(err->error);
      return err->error == 0;
    }
  }

  LOG(ERROR) << "No response from netlink.";
  return false;
}

bool VirtualWIFIManager::Init() {
  nl_->GeNL().SetDefaultHandler([this](nl_msg* m) { HandleNlResponse(m); });
  return RegisterForSimulatorNotifications();
}

VirtualWIFIManager::~VirtualWIFIManager() {
  // Reset handler.
  nl_->GeNL().SetDefaultHandler(std::function<void(nl_msg*)>());
}

void VirtualWIFIManager::HandleNlResponse(nl_msg* m) {
  auto hdr = nlmsg_hdr(m);
  auto gen = static_cast<genlmsghdr*>(nlmsg_data(hdr));

  // Ignore Generic Netlink messages coming from other sources.
  if (hdr->nlmsg_type != nl_->FamilyMAC80211()) return;
  // Ignore Generic Netlink messages that don't contain MAC80211 frames.
  if (gen->cmd != HWSIM_CMD_FRAME) return;

  struct nlattr* attrs[HWSIM_ATTR_MAX + 1];
  if (genlmsg_parse(hdr, 0, attrs, HWSIM_ATTR_MAX, nullptr)) return;

  // Get virtual wlan key from mac address.
  auto mac = attrs[HWSIM_ATTR_ADDR_TRANSMITTER];
  if (!mac) return;
  auto key = MACToKey(nla_data(mac));

  // Redirect packet to VirtualWIFI, if that's indeed one of ours.
  // Sadly, we don't have any other way of telling.
  std::shared_ptr<VirtualWIFI> wifi;
  {
    std::lock_guard<std::mutex> lock(radios_mutex_);
    auto radio = radios_.find(key);
    if (radio == radios_.end()) return;
    wifi = radio->second.lock();
  }

  LOG(INFO) << "Found packet from " << wifi->Name();
}

// Create new MAC80211_HWSIM radio.
// This can be called after Init completes.
std::shared_ptr<VirtualWIFI> VirtualWIFIManager::CreateRadio(
    const std::string& name, const std::string& address) {
  std::shared_ptr<VirtualWIFI> wifi(new VirtualWIFI(nl_, name, address));

  if (!wifi->Init()) {
    wifi.reset();
    return wifi;
  }

  std::lock_guard<std::mutex> lock(radios_mutex_);
  radios_[MACToKey(wifi->MacAddr())] = wifi;
  return wifi;
}

}  // namespace avd
