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

#include <memory>
#include <thread>

#include <netlink/genl/genl.h>
#include "host/commands/wifid/nl_client.h"

namespace avd {
// Netlink provides access to relevant netlink backends and resources.
class Netlink {
 public:
  Netlink();
  ~Netlink() = default;

  // Initialize instance of Netlink Factory.
  bool Init();

  // Getter for NETLINK_GENERIC NlClient instance.
  NlClient& GeNL() { return genl_; }

  // Getter for NETLINK_ROUTE NlClient instance.
  NlClient& RtNL() { return rtnl_; }

  // Access Family ID for MAC80211 (WIFI Simulator).
  int FamilyMAC80211() const { return mac80211_hwsim_family_; }

  // Access Family ID for NL80211 (WIFI management).
  int FamilyNL80211() const { return nl80211_family_; }

 private:
  // Loop and process all incoming netlink messages.
  // This function will trigger calls to NlClient's OnResponse() which handles
  // incoming netlink messages.
  void HandleNetlinkMessages();

  NlClient genl_;
  NlClient rtnl_;

  int mac80211_hwsim_family_ = 0;
  int nl80211_family_ = 0;

  std::unique_ptr<std::thread> netlink_thread_;

  Netlink(const Netlink&) = delete;
  Netlink& operator=(const Netlink&) = delete;
};

}  // namespace avd
