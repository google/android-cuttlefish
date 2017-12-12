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
#include "common/libs/net/network_interface_manager.h"

#include <arpa/inet.h>
#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <memory>

#include "common/libs/glog/logging.h"
#include "common/libs/net/network_interface.h"

namespace avd {
namespace {
std::unique_ptr<NetlinkRequest> BuildLinkRequest(
    const NetworkInterface& interface) {
  auto request = NetlinkRequest::New(NetlinkRequest::RequestType::SetLink);
  request->AddIfInfo(interface.Index(), interface.IsOperational());
  if (!interface.Name().empty()) {
    request->AddString(IFLA_IFNAME, interface.Name());
  }

  return request;
}

std::unique_ptr<NetlinkRequest> BuildAddrRequest(
    const NetworkInterface& interface) {
  auto request = NetlinkRequest::New(NetlinkRequest::RequestType::AddAddress);
  request->AddAddrInfo(interface.Index());
  request->AddInt32(IFA_LOCAL, inet_addr(interface.Address().c_str()));
  request->AddInt32(IFA_ADDRESS, inet_addr(interface.Address().c_str()));
  request->AddInt32(IFA_BROADCAST,
                    inet_addr(interface.BroadcastAddress().c_str()));

  return request;
}
}  // namespace

NetworkInterfaceManager *NetworkInterfaceManager::New(
    NetlinkClient* nl_client) {
  if (nl_client == NULL) {
    LOG(ERROR) << "NetlinkClient is NULL!";
    return NULL;
  }

  return new NetworkInterfaceManager(nl_client);
}

NetworkInterfaceManager::NetworkInterfaceManager(
    NetlinkClient* nl_client)
    : nl_client_(nl_client) {}

NetworkInterface* NetworkInterfaceManager::Open(const std::string& if_name) {
  const int32_t index = nl_client_->NameToIndex(if_name);
  if (index < 0) {
    LOG(ERROR) << "Failed to get interface (" << if_name << ") index.";
    return NULL;
  }

  return new NetworkInterface(index);
}

bool NetworkInterfaceManager::ApplyChanges(const NetworkInterface& iface) {
  auto request = BuildLinkRequest(iface);
  if (!nl_client_->Send(request.get())) return false;

  // Terminate immediately if interface is down.
  if (!iface.IsOperational()) return true;

  request = BuildAddrRequest(iface);
  return nl_client_->Send(request.get());
}

}  // namespace avd

