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
  LOG(INFO) << "Netlink response received." << m;
}

// Create new MAC80211_HWSIM radio.
// This can be called after Init completes.
std::shared_ptr<VirtualWIFI> VirtualWIFIManager::CreateRadio(
    const std::string& name) {
  std::shared_ptr<VirtualWIFI> wifi(new VirtualWIFI(nl_, name));

  if (!wifi->Init()) {
    wifi.reset();
    return wifi;
  }

  std::lock_guard<std::mutex> lock(radios_mutex_);
  radios_[wifi->HwSimNumber()] = wifi;
  return wifi;
}

}  // namespace avd
