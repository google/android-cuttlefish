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
#include <string>

#include "host/commands/wifid/netlink.h"

namespace avd {

// VirtualWIFI is an abstraction of an (individual) virtual WLAN device.
// A virtual WLAN is a composition of the three elements:
// - HWSIM RADIO, or an instance of a virtual MAC80211 device; this instance is
//   later used to determine origin of the 802.11 frames (ie. which virtual
//   interface was used to send them),
// - WIPHY, or Radio that is recognized by Linux kernel; these instances are
//   *named* representations of the HWSIM radios and can be used to identify
//   associated WLAN interface,
// - WLAN, or WIFI Interface, which is directly used network stack and tools.
//
// Typically, Cuttlefish guests will run with just one VirtualWIFI instance, but
// the host will need (typically) one per Guest instance. This is dictated by
// the fact that at most one user-space daemon can listen for MAC80211 packets
// at any given time.
class VirtualWIFI {
 public:
  VirtualWIFI(Netlink* nl, const std::string& name) : nl_(nl), name_(name) {}
  ~VirtualWIFI();

  int HwSimNumber() const { return hwsim_number_; }

  bool Init();

 private:
  Netlink* nl_;
  std::string name_;

  int hwsim_number_ = 0;
  int wiphy_number_ = 0;
  int iface_number_ = 0;

  VirtualWIFI(const VirtualWIFI&) = delete;
  VirtualWIFI& operator=(const VirtualWIFI&) = delete;
};

}  // namespace avd
