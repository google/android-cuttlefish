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
#include "guest/gce_network/network_interface_manager.h"

#include <arpa/inet.h>
#include <linux/if_link.h>

#include <memory>

#include "guest/gce_network/network_interface.h"
#include "guest/gce_network/network_namespace_manager.h"

namespace avd {

// VETH and IF constants, taken from linux/veth.h and linux/if_link.h
// To break direct dependency from linux headers, which conflict with bionic
// headers, we keep a copy of these values here.
// Names have been modified to avoid potential conflict with future Android
// releases.
enum {
  kVEth_Info_Unspec,
  kVEth_Info_Peer,
};

enum {
  kIFLA_Unspec,
  kIFLA_Address,
  kIFLA_Broadcast,
  kIFLA_IfName,
  kIFLA_MTU,
  kIFLA_Link,
  kIFLA_QDisc,
  kIFLA_Stats,
  kIFLA_Cost,
  kIFLA_Priority,
  kIFLA_Master,
  kIFLA_Wireless,
  kIFLA_ProtInfo,
  kIFLA_TxQLen,
  kIFLA_Map,
  kIFLA_Weight,
  kIFLA_OperState,
  kIFLA_LinkMode,
  kIFLA_LinkInfo,
  kIFLA_NetNsPID,
  kIFLA_IfAlias,
  kIFLA_NumVF,
  kIFLA_VFInfoList,
  kIFLA_Stats64,
  kIFLA_VfPorts,
  kIFLA_PortSelf,
  kIFLA_AFSpec,
  kIFLA_Group,
  kIFLA_NetNsFD,
  kIFLA_ExtMask,
  kIFLA_Promiscuity,
  kIFLA_NumTxQueues,
  kIFLA_NumRxQueues,
  kIFLA_Carrier,
  kIFLA_PhysPortId,
  kIFLA_CarrierChanges,
};

enum {
  kIFLA_Info_Unspec,
  kIFLA_Info_Kind,
  kIFLA_Info_Data,
  kIFLA_Info_XStats,
  kIFLA_Info_SlaveKind,
  kIFLA_Info_SlaveData,
};

namespace {
// Virtual interface kind. Used to create new VETH pairs.
const char kVethLinkKind[] = "veth";
}  // namespace

NetworkInterfaceManager *NetworkInterfaceManager::New(
    NetlinkClient* nl_client, NetworkNamespaceManager* namespace_manager) {
  if (nl_client == NULL) {
    KLOG_ERROR(LOG_TAG, "%s: NetlinkClient is NULL!\n", __FUNCTION__);
    return NULL;
  }

  if (namespace_manager == NULL) {
    KLOG_ERROR(LOG_TAG, "%s: NetworkNamespaceManager is NULL!\n", __FUNCTION__);
    return NULL;
  }

  return new NetworkInterfaceManager(nl_client, namespace_manager);
}

NetworkInterfaceManager::NetworkInterfaceManager(
    NetlinkClient* nl_client, NetworkNamespaceManager* namespace_manager)
    : nl_client_(nl_client),
      ns_manager_(namespace_manager) {}

NetworkInterface* NetworkInterfaceManager::Open(const std::string& if_name) {
  const int32_t index = nl_client_->NameToIndex(if_name);
  if (index < 0) {
    KLOG_ERROR(LOG_TAG, "%s:%d: Failed to get interface (%s) index.\n",
               __FILE__, __LINE__, if_name.c_str());
    return NULL;
  }

  return new NetworkInterface(index);
}

bool NetworkInterfaceManager::CreateVethPair(
    const NetworkInterface& veth1, const NetworkInterface& veth2) {
  // The IFLA structure is not linear and can carry multiple embedded chunks.
  // This is the case when we create a new link.
  // IFLA_LINKINFO contains a substructure describing the link.
  // Each IFLA tag has associated data length. In order to provide the length of
  // the structure, we build substructures directly inside the buffer, and later
  // update the length.
  // Structure looks like this:
  //
  // RTM_NEWLINK[
  //   [ ... interface 1 details ... ]
  //   IFLA_LINKINFO[
  //     length,
  //     IFLA_INFO_KIND[length, "veth"],
  //     IFLA_INFO_DATA[
  //       length,
  //       VETH_INFO_PEER[
  //         length,
  //         [ ... interface 2 details ... ]
  //       ],
  //     ],
  //   ],
  // ]
  //
  std::unique_ptr<NetlinkRequest> request(nl_client_->CreateRequest(true));

  if (!request.get()) return false;
  if (!BuildRequest(request.get(), veth1)) return false;
  request->PushList(kIFLA_LinkInfo);
  request->AddString(kIFLA_Info_Kind, kVethLinkKind);
  request->PushList(kIFLA_Info_Data);
  request->PushList(kVEth_Info_Peer);
  if (!BuildRequest(request.get(), veth2)) return false;

  request->PopList();  // kVEth_Info_Peer
  request->PopList();  // kIFLA_Info_Data
  request->PopList();  // kIFLA_LinkInfo

  return nl_client_->Send(request.get());
}

bool NetworkInterfaceManager::ApplyChanges(const NetworkInterface& iface) {
  std::unique_ptr<NetlinkRequest> request(nl_client_->CreateRequest(false));
  if (!BuildRequest(request.get(), iface)) return false;
  return nl_client_->Send(request.get());
}

// private
bool NetworkInterfaceManager::BuildRequest(
    NetlinkRequest* request, const NetworkInterface& interface) {
  request->AddIfInfo(interface.index());

  // The following changes are idempotent, e.g. changing interface name to
  // itself is essentially a no-op.
  if (!interface.name().empty()) {
    request->AddString(kIFLA_IfName, interface.name());
  }

  // Network namespace.
  if (!interface.network_namespace().empty()) {
    int32_t fd = ns_manager_->GetNamespaceDescriptor(
        interface.network_namespace());
    if (fd < 0) return false;
    request->AddInt32(kIFLA_NetNsFD, fd);
  }

  return true;
}

}  // namespace avd

