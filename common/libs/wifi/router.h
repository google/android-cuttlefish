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

namespace cvd {
// Commands recognized by WIFIRouter netlink family.
enum {
  // WIFIROUTER_CMD_REGISTER is used by client to request notifications for
  // packets sent from an interface with specific MAC address. Recognized
  // attributes:
  // - WIFIROUTER_ATTR_HWSIM_ID - ID of HWSIM card to receive notifications for,
  // - WIFIROUTER_ATTR_HWSIM_ADDR - MAC address (byte array) of interface to
  //   receive notifications for.
  WIFIROUTER_CMD_REGISTER,

  // WIFIROUTER_CMD_NOTIFY is issued by the server to notify clients for every
  // new WIFIROUTER packet the client is registered for. Comes with attributes:
  // - WIFIROUTER_ATTR_HWSIM_ID - MAC address of interface that received packet,
  // - WIFIROUTER_ATTR_PACKET - content of the MAC80211_HWSIM packet.
  WIFIROUTER_CMD_NOTIFY,

  // WIFIROUTER_RMD_SEND is issued by the client to request injection of a
  // packet to all interfaces the client is registered for. Comes with
  // attributes:
  // - WIFIROUTER_ATTR_PACKET - content of the MAC80211_HWSIM packet.
  WIFIROUTER_CMD_SEND,
};

// Attributes recognized by WIFIRouter netlink family.
enum {
  // Don't use attribute 0 to avoid parsing malformed message.
  WIFIROUTER_ATTR_UNSPEC,

  // MAC address representing interface from which the packet originated.
  WIFIROUTER_ATTR_HWSIM_ID,

  // Physical address of wireless interface.
  WIFIROUTER_ATTR_HWSIM_ADDR,

  // MAC80211_HWSIM packet content.
  WIFIROUTER_ATTR_PACKET,

  // Keep this last.
  WIFIROUTER_ATTR_MAX
};

}  // namespace cvd
