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

namespace cuttlefish {

// Abstraction of Netlink client class.
class NetlinkClient {
 public:
  NetlinkClient() {}
  virtual ~NetlinkClient() {}

  // Send netlink message to kernel.
  virtual bool Send(const NetlinkRequest& message) = 0;

 private:
  NetlinkClient(const NetlinkClient&);
  NetlinkClient& operator= (const NetlinkClient&);
};

class NetlinkClientFactory {
 public:
  // Create new NetlinkClient instance of a specified type.
  // type can be any of the NETLINK_* types (eg. NETLINK_ROUTE).
  virtual std::unique_ptr<NetlinkClient> New(int type) = 0;

  static NetlinkClientFactory* Default();

 protected:
  NetlinkClientFactory() = default;
  virtual ~NetlinkClientFactory() = default;
};

}  // namespace cuttlefish

#endif  // COMMON_LIBS_NET_NETLINK_CLIENT_H_
