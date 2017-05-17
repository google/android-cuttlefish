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
#ifndef GCE_NETWORK_NETWORK_INTERFACE_H_
#define GCE_NETWORK_NETWORK_INTERFACE_H_

#include <string>

namespace avd {

// Abstraction of network interfaces.
// This interface provides means to modify network interface parameters.
class NetworkInterface {
 public:
  explicit NetworkInterface(size_t if_index)
      : if_index_(if_index) {}

  NetworkInterface()
      : if_index_(0) {}

  virtual ~NetworkInterface() {}

  // Get network interface index.
  size_t index() const {
    return if_index_;
  }

  // Set name of the network interface.
  virtual NetworkInterface& set_name(const std::string& new_name) {
    name_ = new_name;
    return *this;
  }

  // Get name of the network interface.
  // Returns name, if previously set.
  virtual const std::string& name() const {
    return name_;
  }

  // Set namespace of the network interface.
  virtual NetworkInterface& set_network_namespace(
      const std::string& new_namespace) {
    network_namespace_ = new_namespace;
    return *this;
  }

  // Get namespace of the network interface.
  // Returns namespace, if previously set.
  virtual const std::string& network_namespace() const {
    return network_namespace_;
  }

 private:
  // Index of the network interface in the system table. 0 indicates new
  // interface.
  size_t if_index_;
  // Name of the interface, e.g. "eth0".
  std::string name_;
  // Name of the namespace the interface belongs to.
  std::string network_namespace_;

  NetworkInterface(const NetworkInterface&);
  NetworkInterface& operator= (const NetworkInterface&);
};

}  // namespace avd

#endif  // GCE_NETWORK_NETWORK_INTERFACE_H_
