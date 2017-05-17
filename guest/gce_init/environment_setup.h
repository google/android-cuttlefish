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
#ifndef GCE_INIT_ENVIRONMENT_SETUP_H_
#define GCE_INIT_ENVIRONMENT_SETUP_H_

#include <string>

#include <gce_network/dhcp_server.h>
#include <gce_network/logging.h>
#include <gce_network/metadata_proxy.h>
#include <gce_network/namespace_aware_executor.h>
#include <gce_network/network_interface_manager.h>
#include <gce_network/network_namespace_manager.h>
#include <gce_network/sys_client.h>

namespace avd {

class EnvironmentSetup {
 public:
  EnvironmentSetup(
      NamespaceAwareExecutor* executor,
      NetworkNamespaceManager* ns_manager,
      NetworkInterfaceManager* if_manager,
      SysClient* sys_client)
      : executor_(executor),
        ns_manager_(ns_manager),
        if_manager_(if_manager),
        sys_client_(sys_client) {}

  ~EnvironmentSetup() {}

  // Configure Cloud Android network.
  bool ConfigureNetworkCommon();
  bool ConfigureNetworkMobile();
  bool ConfigurePortForwarding();
  bool CreateNamespaces();

 private:
  // Create new, simple DHCP server.
  // DHCP server will use supplied |options| to identify interface used to
  // supply configuration to its clients.
  void CreateDhcpServer(
      const std::string& namespace_name,
      const DhcpServer::Options& options);

  // Create metadata proxy.
  // Metadata proxy fetches metadata updates from GCE metadata server and serves
  // them (if change is detected) to all subscribed clients.
  // Metadata proxy uses unix socket to provide metadata access to all
  // interested processes.
  void CreateMetadataProxy();

  NamespaceAwareExecutor* executor_;
  NetworkNamespaceManager* ns_manager_;
  NetworkInterfaceManager* if_manager_;
  SysClient* sys_client_;

  EnvironmentSetup(const EnvironmentSetup&);
  EnvironmentSetup& operator= (const EnvironmentSetup&);
};

}  // namespace avd

#endif  // GCE_INIT_ENVIRONMENT_SETUP_H_
