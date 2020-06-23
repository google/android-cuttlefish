/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/libs/net/netlink_client.h"
#include "common/libs/net/network_interface.h"
#include "common/libs/net/network_interface_manager.h"

#include <net/if.h>
#include <cstdio>

// This command can only rename network interfaces that are *DOWN*

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s [ethA] [ethB]\n", argv[0]);
    return -1;
  }
  const char *const name = argv[1];
  int32_t index = if_nametoindex(name);
  if (index == 0) {
    fprintf(stderr, "%s: invalid interface name '%s'\n", argv[0], name);
    return -2;
  }
  const char *const new_name = argv[2];
  auto factory = cuttlefish::NetlinkClientFactory::Default();
  std::unique_ptr<cuttlefish::NetlinkClient> nl(factory->New(NETLINK_ROUTE));
  std::unique_ptr<cuttlefish::NetworkInterfaceManager> nm(
      cuttlefish::NetworkInterfaceManager::New(factory));
  std::unique_ptr<cuttlefish::NetworkInterface> ni(nm->Open(new_name, name));
  bool res = false;
  if (ni) {
    ni->SetName(new_name);
    res = nm->ApplyChanges(*ni);
  }
  if (!res) {
    fprintf(stderr, "%s: renaming interface '%s' failed\n", argv[0], name);
    return -3;
  }
  return 0;
}
