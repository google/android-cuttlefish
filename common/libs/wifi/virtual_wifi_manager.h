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
#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "host/commands/wifid/netlink.h"
#include "host/commands/wifid/virtual_wifi.h"

namespace avd {
class VirtualWIFIManager {
 public:
  VirtualWIFIManager(Netlink* nl) : nl_(nl){};
  ~VirtualWIFIManager();

  // Initialize VirtualWIFI Manager instance.
  bool Init();

  // Create new VirtualWIFI instance with the specified name.
  std::shared_ptr<VirtualWIFI> CreateRadio(const std::string& name,
                                           const std::string& address);

 private:
  // Enables asynchronous notifications from MAC80211 about recently sent wifi
  // packets.
  bool RegisterForSimulatorNotifications();

  // Handle asynchronous netlink frame.
  // Netlink does not differentiate between frame types so this callback will
  // receive all Generic Netlink frames that do not have a proper recipient.
  void HandleNlResponse(nl_msg* m);

  Netlink* const nl_;

  // Map VirtualWIFI's MAC address to VirtualWIFI instance.
  std::map<uint64_t, std::weak_ptr<VirtualWIFI>> radios_;
  std::mutex radios_mutex_;

  VirtualWIFIManager(const VirtualWIFIManager&) = delete;
  VirtualWIFIManager& operator=(const VirtualWIFIManager&) = delete;
};

}  // namespace avd
