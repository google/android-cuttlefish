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

#include <netinet/in.h>
#include <linux/netdevice.h>

#include "common/libs/wifi/netlink.h"

namespace cvd {

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
  VirtualWIFI(Netlink* nl, const std::string& name, const std::string& macaddr)
      : nl_(nl), name_(name), addr_(macaddr) {}
  ~VirtualWIFI();

  const uint8_t* MacAddr() const { return &mac_addr_[0]; }
  const std::string& Name() const { return name_; }

  bool Init();

 private:
  Netlink* nl_;
  std::string name_;

  // MAC address associated with primary WLAN interface.
  // This is the only way to identify origin of the packets.
  // Sadly, if MAC Address is altered manually at runtime, we
  // will stop working.
  std::string addr_;

  // NOTE: this has to be MAX_ADDR_LEN, even if we occupy fewer bytes.
  // Netlink requires this to be full length.
  uint8_t mac_addr_[MAX_ADDR_LEN];

  // HWSIM number is required to identify HWSIM device that we want destroyed
  // when we no longer need it.
  int hwsim_number_ = 0;

  // WIPHY and WIFI interface numbers. Useful for local operations, such as
  // renaming interface.
  int wiphy_number_ = 0;
  int iface_number_ = 0;

  VirtualWIFI(const VirtualWIFI&) = delete;
  VirtualWIFI& operator=(const VirtualWIFI&) = delete;
};

}  // namespace cvd
