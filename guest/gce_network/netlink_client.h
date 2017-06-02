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
#ifndef GUEST_GCE_NETWORK_NETLINK_CLIENT_H_
#define GUEST_GCE_NETWORK_NETLINK_CLIENT_H_

#include <stddef.h>

#include <string>

#include "guest/gce_network/sys_client.h"

namespace avd {

// Abstraction of Network link request.
// Used to supply kernel with information about which interface needs to be
// changed, and how.
class NetlinkRequest {
 public:
  NetlinkRequest() {}
  virtual ~NetlinkRequest() {}

  // Add an IFLA tag followed by a string.
  // Returns true, if successful.
  virtual void AddString(uint16_t type, const std::string& value) = 0;

  // Add an IFLA tag followed by int32.
  // Returns true, if successful.
  virtual void AddInt32(uint16_t type, int32_t value) = 0;

  // Add an interface info structure.
  // Parameter |if_index| specifies particular interface index to which the
  // attributes following the IfInfo apply.
  virtual void AddIfInfo(int32_t if_index) = 0;

  // Creates new list.
  // List mimmic recursive structures in a flat, contiuous representation.
  // Each call to PushList() should have a corresponding call to PopList
  // indicating end of sub-attribute list.
  virtual void PushList(uint16_t type) = 0;

  // Indicates end of previously declared list.
  virtual void PopList() = 0;

  // Request data.
  virtual void* RequestData() = 0;

  // Request length.
  virtual size_t RequestLength() = 0;

  // Request Sequence Number.
  virtual uint32_t SeqNo() = 0;

 private:
  NetlinkRequest(const NetlinkRequest&);
  NetlinkRequest& operator= (const NetlinkRequest&);
};

// Abstraction of Netlink client class.
class NetlinkClient {
 public:
  NetlinkClient() {}
  virtual ~NetlinkClient() {}

  // Get interface index.
  // Returns 0 if interface does not exist.
  virtual int32_t NameToIndex(const std::string& name) = 0;

  // Create new Netlink Request structure.
  // When |create_new_interface| is true, the request will create a new,
  // not previously existing interface.
  virtual NetlinkRequest* CreateRequest(bool create_new_interface) = 0;

  // Send netlink message to kernel.
  virtual bool Send(NetlinkRequest* message) = 0;

  // Create default instance of NetlinkClient.
  static NetlinkClient* New(SysClient* sys_client);

 private:
  NetlinkClient(const NetlinkClient&);
  NetlinkClient& operator= (const NetlinkClient&);
};

}  // namespace avd

#endif  // GUEST_GCE_NETWORK_NETLINK_CLIENT_H_
