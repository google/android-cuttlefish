/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"

namespace cvd {
// Creates, or connects to if it already exists, a tap network interface. The
// user needs CAP_NET_ADMIN to create such interfaces or be the owner to connect
// to one.
SharedFD OpenTapInterface(const std::string& interface_name);

// Returns a list of TAP devices that have open file descriptors
std::set<std::string> TapInterfacesInUse();

struct DnsmasqDhcp4Lease {
  std::uint64_t expiry;
  std::uint8_t mac_address[6];
  std::uint8_t ip_address[4];
  std::string hostname;
  std::string client_id;
};

// Parses a dnsmasq lease file
std::vector<DnsmasqDhcp4Lease> ParseDnsmasqLeases(SharedFD lease_file);

std::ostream& operator<<(std::ostream&, const DnsmasqDhcp4Lease&);

// Sends a DHCPRELEASE message over the socket;
bool ReleaseDhcp4(SharedFD tap, const std::uint8_t mac_address[6],
                  const std::uint8_t ip_address[4],
                  const std::uint8_t dhcp_server_ip[4]);

}
