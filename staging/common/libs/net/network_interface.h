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
#ifndef GUEST_GCE_NETWORK_NETWORK_INTERFACE_H_
#define GUEST_GCE_NETWORK_NETWORK_INTERFACE_H_

#include <string>

namespace cuttlefish {

// Abstraction of network interfaces.
// This interface provides means to modify network interface parameters.
class NetworkInterface {
 public:
  explicit NetworkInterface(size_t if_index)
      : if_index_(if_index) {}

  NetworkInterface() = default;
  ~NetworkInterface() = default;

  // Get network interface index.
  size_t Index() const {
    return if_index_;
  }

  // Set name of the network interface.
  NetworkInterface& SetName(const std::string& new_name) {
    name_ = new_name;
    return *this;
  }

  // Get name of the network interface.
  // Returns name, if previously set.
  const std::string& Name() const {
    return name_;
  }

  int PrefixLength() const {
    return prefix_len_;
  }

  // Set operational state of the network interface (ie. whether interface is
  // up).
  NetworkInterface& SetOperational(bool is_operational) {
    is_operational_ = is_operational;
    return *this;
  }

  // Get operational state of the interface. Value of 'true' indicates interface
  // should be 'up'.
  bool IsOperational() const {
    return is_operational_;
  }

  // Set IPv4 address of the network interface.
  NetworkInterface& SetAddress(const std::string& address) {
    ip_address_ = address;
    return *this;
  }

  // Get IPv4 address of the network interface.
  const std::string& Address() const {
    return ip_address_;
  }

  // Set IPv4 broadcast address of the network interface.
  NetworkInterface& SetBroadcastAddress(const std::string& address) {
    bc_address_ = address;
    return *this;
  }

  // Set IPv4 prefix length
  NetworkInterface& SetPrefixLength(int len) {
    prefix_len_ = len;
    return *this;
  }

  // Get IPv4 broadcast address of the network interface.
  const std::string& BroadcastAddress() const {
    return bc_address_;
  }

 private:
  // Index of the network interface in the system table. 0 indicates new
  // interface.
  size_t if_index_ = 0;
  // Name of the interface, e.g. "eth0".
  std::string name_;
  // Operational status, i.e. whether interface is up.
  bool is_operational_ = false;
  // IPv4 address of this interface.
  std::string ip_address_;
  // IPv4 broadcast address of this interface.
  std::string bc_address_;
  // IPv4 prefix (aka netmask. 0 means use the default)
  int prefix_len_ = 24;

  NetworkInterface(const NetworkInterface&);
  NetworkInterface& operator= (const NetworkInterface&);
};

}  // namespace cuttlefish

#endif  // GUEST_GCE_NETWORK_NETWORK_INTERFACE_H_
