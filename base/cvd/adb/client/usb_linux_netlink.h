/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <string>
#include <string_view>
#include <unordered_map>

#include <sys/types.h>

// Helper class to parse netlink messages.

// A message is a sequence of null-terminated strings as follows.
// add@/devices/pci0000:00/0000:00:03.1/0000:02:00.0/0000:03:08.0/0000:04:00.1/usb4/4-1
// ACTION=add
// DEVPATH=/devices/pci0000:00/0000:00:03.1/0000:02:00.0/0000:03:08.0/0000:04:00.1/usb4/4-1
// SUBSYSTEM=usb
// MAJOR=189
// MINOR=509
// DEVNAME=bus/usb/004/126
// DEVTYPE=usb_device
// PRODUCT=18d1/4ee7/601
// TYPE=0/0/0
// BUSNUM=004
// DEVNUM=126
// SEQNUM=10453
//
// This class only capture key/value pairs, the "header@" is ignored.
class NetlinkMessage {
  public:
    NetlinkMessage(const char* buffer, ssize_t len);

    // Return the value for the given key. If the key is not found, return an empty string.
    std::string attr(const std::string& key) const;

    // Return true if the message has the given attribute with the given value.
    bool has_attr(const std::string& key, std::string_view value) const;

  private:
    std::unordered_map<std::string, std::string> attrs_;
};