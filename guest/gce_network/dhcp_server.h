/*
 * Copyright (C) 2016 The Android Open Source Project
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
#ifndef GCE_NETWORK_DHCP_SERVER_H_
#define GCE_NETWORK_DHCP_SERVER_H_

#include <string>

#include "jb_compat.h"

namespace avd {

// Abstraction of the DHCP server class.
class DhcpServer {
 public:
  struct Options {
    Options()
        : mtu(0),
          lease_time(0) {}

    static const uint32_t kLeaseTimeInfinite = ~0;

    // Specify device to bind service to.
    Options& set_bind_device(const std::string& device) {
      bind_device = device;
      return *this;
    }

    // Specify the IP address at which the server is located.
    Options& set_server_address(const std::string& address) {
      server_address = address;
      return *this;
    }

    // Specify gateway IP address that will be offered to dhcp clients.
    Options& set_gateway_address(const std::string& address) {
      gateway_address = address;
      return *this;
    }

    // Specify range of IP addresses that will be offered to clients.
    Options& set_start_ip_address(const std::string& address) {
      start_ip_address = address;
      return *this;
    }

    Options& set_end_ip_address(const std::string& address) {
      end_ip_address = address;
      return *this;
    }

    // Specify network mask that will be used by the DHCP server.
    Options& set_network_mask(const std::string& mask) {
      network_mask = mask;
      return *this;
    }

    // Specify DNS IP address that will be offered to clients.
    Options& set_dns_address(const std::string& dns) {
      dns_address = dns;
      return *this;
    }

    // Set Max Transfer Unit that will be shared with the client.
    Options& set_mtu(int32_t size) {
      mtu = size;
      return *this;
    }

    // Set expiration time for the DHCP leases.
    Options& set_lease_time(uint32_t seconds) {
      lease_time = seconds;
      return *this;
    }

    std::string bind_device;
    std::string server_address;
    std::string gateway_address;
    std::string start_ip_address;
    std::string end_ip_address;
    std::string network_mask;
    std::string dns_address;
    int32_t mtu;
    uint32_t lease_time;
  };

  DhcpServer() {}
  virtual ~DhcpServer() {}

  // Start listening for incoming DHCP requests.
  virtual bool Start(const Options& options) = 0;

  // Create default instance of the DHCP server.
  static DhcpServer* New();

 private:
  DhcpServer(const DhcpServer&);
  DhcpServer& operator= (const DhcpServer&);
};
}  // namespace avd

#endif  // GCE_NETWORK_DHCP_SERVER_H_
