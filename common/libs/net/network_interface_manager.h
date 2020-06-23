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
#ifndef COMMON_LIBS_NET_NETWORK_INTERFACE_MANAGER_H_
#define COMMON_LIBS_NET_NETWORK_INTERFACE_MANAGER_H_

#include <memory>
#include <string>

#include "common/libs/net/netlink_client.h"
#include "common/libs/net/network_interface.h"

namespace cuttlefish {

// Network interface manager class.
// - Provides access for existing network interfaces,
// - provides means to create new virtual interfaces.
//
// Example usage:
//
//   std::unique_ptr<NetlinkClient> client(NetlinkClient::GetDefault());
//   NetworkInterfaceManager manager(client.get());
//   std::unique_ptr<NetworkInterface> iface(manager.Open("eth0", "em0"));
//
class NetworkInterfaceManager {
 public:
  // Open existing network interface.
  //
  // NOTE: this method does not fill in any NetworkInterface details yet.
  std::unique_ptr<NetworkInterface> Open(const std::string& if_name,
                                         const std::string& if_name_alt);

  // Apply changes made to existing network interface.
  // This method cannot be used to instantiate new network interfaces.
  bool ApplyChanges(const NetworkInterface& interface);

  // Create new connected pair of virtual (veth) interfaces.
  // Supplied pair of interfaces describe both endpoints' properties.
  bool CreateVethPair(const NetworkInterface& first,
                      const NetworkInterface& second);

  // Creates new NetworkInterfaceManager.
  static std::unique_ptr<NetworkInterfaceManager> New(
      NetlinkClientFactory* factory);

 private:
  NetworkInterfaceManager(std::unique_ptr<NetlinkClient> nl_client);

  // Build (partial) netlink request.
  bool BuildRequest(NetlinkRequest* request, const NetworkInterface& interface);

  std::unique_ptr<NetlinkClient> nl_client_;

  NetworkInterfaceManager(const NetworkInterfaceManager&);
  NetworkInterfaceManager& operator= (const NetworkInterfaceManager&);
};

}  // namespace cuttlefish

#endif  // COMMON_LIBS_NET_NETWORK_INTERFACE_MANAGER_H_
