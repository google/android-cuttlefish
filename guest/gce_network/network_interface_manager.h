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
#ifndef GUEST_GCE_NETWORK_NETWORK_INTERFACE_MANAGER_H_
#define GUEST_GCE_NETWORK_NETWORK_INTERFACE_MANAGER_H_

#include <string>

#include "guest/gce_network/logging.h"
#include "guest/gce_network/netlink_client.h"
#include "guest/gce_network/network_interface.h"
#include "guest/gce_network/network_namespace_manager.h"

namespace avd {

// Network interface manager class.
// - Provides access for existing network interfaces,
// - provides means to create new virtual interfaces.
//
// Example usage:
//
//   std::unique_ptr<NetlinkClient> client(NetlinkClient::GetDefault());
//   std::unique_ptr<NetworkNamespaceManager> ns_mgr(
//       NetworkNamespaceManager::GetDefault());
//
//   NetworkInterfaceManager manager(client.get(), ns_mgr.get());
//   std::unique_ptr<NetworkInterface> iface(manager.Open("eth0"));
//
class NetworkInterfaceManager {
 public:
  // Open existing network interface.
  //
  // TODO(ender): this method does not fill in any NetworkInterface details.
  NetworkInterface* Open(const std::string& if_name);

  // Apply changes made to existing network interface.
  // This method cannot be used to instantiate new network interfaces.
  bool ApplyChanges(const NetworkInterface& interface);

  // Create new connected pair of virtual (veth) interfaces.
  // Supplied pair of interfaces describe both endpoints' properties.
  bool CreateVethPair(const NetworkInterface& first,
                      const NetworkInterface& second);

  // Creates new NetworkInterfaceManager.
  // Returns NULL if parameters are invalid.
  static NetworkInterfaceManager* New(
      NetlinkClient* nl_client, NetworkNamespaceManager* namespace_manager);

 private:
  NetworkInterfaceManager(
      NetlinkClient* nl_client, NetworkNamespaceManager* namespace_manager);

  // Build (partial) netlink request.
  bool BuildRequest(NetlinkRequest* request, const NetworkInterface& interface);

  NetlinkClient* nl_client_;
  NetworkNamespaceManager* ns_manager_;

  NetworkInterfaceManager(const NetworkInterfaceManager&);
  NetworkInterfaceManager& operator= (const NetworkInterfaceManager&);
};

}  // namespace avd

#endif  // GUEST_GCE_NETWORK_NETWORK_INTERFACE_MANAGER_H_
