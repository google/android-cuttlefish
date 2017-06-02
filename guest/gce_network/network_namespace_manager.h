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
#ifndef GUEST_GCE_NETWORK_NETWORK_NAMESPACE_MANAGER_H_
#define GUEST_GCE_NETWORK_NETWORK_NAMESPACE_MANAGER_H_

#include <stdint.h>
#include <string>

#include "guest/gce_network/sys_client.h"

namespace avd {
// Network namespace manager.
// Use this to create new namespaces or acquire descriptors of existing ones.
//
// Example:
//   UniquePtr<NetworkNamespaceManager> ns_manager(
//       NetworkNamespaceManager::New());
//   if (ns_manager.get()) {
//     ...
//   }
//
class NetworkNamespaceManager {
 public:
  // Namespace names:
  // - kAndroidNs is occupied by Android OS,
  // - kOuterNs is occupied by GCE.
  static const char kAndroidNs[];
  static const char kOuterNs[];

  NetworkNamespaceManager() {}
  virtual ~NetworkNamespaceManager() {}

  // Creates new isolated namespace.
  // Isolated namespaces come only with unconfigured basic network interfaces.
  // |create_new_namespace| indicates that the new namespace should be created,
  // otherwise function creates alias to the current network namespace.
  // When |is_paranoid| is set, all Android Paranoid Network restrictions apply.
  virtual bool CreateNetworkNamespace(
      const std::string& ns_name,
      bool create_new_namespace, bool is_paranoid) = 0;

  // Acquire file descriptor pointing to specific network namespace.
  virtual int32_t GetNamespaceDescriptor(const std::string& ns_name) = 0;

  // Switch current namespace to namespace indicated by ns_name.
  virtual bool SwitchNamespace(const std::string& ns_name) = 0;

  // Instantiate new NetworkNamespaceManager.
  // Caller retains ownership of supplied classes.
  // Return NULL, if instantiation failed.
  static NetworkNamespaceManager* New(SysClient* sys_client);

 private:
  NetworkNamespaceManager(const NetworkNamespaceManager&);
  NetworkNamespaceManager& operator= (const NetworkNamespaceManager&);
};

}  // namespace avd

#endif  // GUEST_GCE_NETWORK_NETWORK_NAMESPACE_MANAGER_H_
