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
#ifndef COMMON_LIBS_NET_NETLINK_CLIENT_H_
#define COMMON_LIBS_NET_NETLINK_CLIENT_H_

#include <stddef.h>
#include <memory>
#include <string>
#include "common/libs/net/netlink_request.h"

namespace avd {

// Abstraction of Netlink client class.
class NetlinkClient {
 public:
  NetlinkClient() {}
  virtual ~NetlinkClient() {}

  // Get interface index.
  // Returns 0 if interface does not exist.
  virtual int32_t NameToIndex(const std::string& name) = 0;

  // Send netlink message to kernel.
  virtual bool Send(NetlinkRequest* message) = 0;

  // Create default instance of NetlinkClient.
  static std::unique_ptr<NetlinkClient> New();

 private:
  NetlinkClient(const NetlinkClient&);
  NetlinkClient& operator= (const NetlinkClient&);
};

}  // namespace avd

#endif  // COMMON_LIBS_NET_NETLINK_CLIENT_H_
