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
#ifndef GUEST_GCE_NETWORK_METADATA_PROXY_H_
#define GUEST_GCE_NETWORK_METADATA_PROXY_H_

#include <string>

#include "guest/gce_network/network_namespace_manager.h"
#include "guest/gce_network/sys_client.h"

namespace avd {

class MetadataProxy {
 public:
  // Create default instance of the Metadata Proxy.
  static MetadataProxy* New(
      SysClient* client,
      NetworkNamespaceManager* ns_manager);

  MetadataProxy() {}
  virtual ~MetadataProxy() {}

  // Start proxying metadata updates to Unix socket named <socket_name>.
  virtual bool Start(const std::string& socket_name) = 0;

 private:
  MetadataProxy(const MetadataProxy&);
  MetadataProxy& operator= (const MetadataProxy&);
};

}  // namespace avd

#endif  // GUEST_GCE_NETWORK_METADATA_PROXY_H_
