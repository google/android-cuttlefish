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

#define LOG_TAG "wifi_setup"

#include "guest_bridge.h"

#include <net/if.h>
#include <cstdio>
#include <log/log.h>
#include "common/libs/net/netlink_client.h"
#include "common/libs/net/network_interface.h"
#include "common/libs/net/network_interface_manager.h"
#include "common/libs/fs/shared_fd.h"

int bridge_interface(const cvd::SharedFD& bfd, const char* bridge_name,
                     uint32_t slave_index, const char* name) {
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, bridge_name, sizeof(ifr.ifr_name));
  ifr.ifr_ifindex = slave_index;
  if (bfd->Ioctl(SIOCBRADDIF, &ifr) == -1) {
    ALOGE("unable to add %d (%s) to bridge (%s)", (int)slave_index, name,
          bfd->StrError());
    return -1;
  }
  return 0;
}

int make_bridge(const char* eth_name, const char* wifi_name) {
  int32_t data_index = if_nametoindex(eth_name);
  if (data_index == 0) {
    ALOGE("invalid data interface name '%s'", eth_name);
    return 2;
  }
  std::string ap_name(wifi_name);
  ap_name+="_ap";
  std::string data_name(wifi_name);
  data_name+="_data";
  auto factory = cvd::NetlinkClientFactory::Default();
  std::unique_ptr<cvd::NetlinkClient> nl(factory->New(NETLINK_ROUTE));
  std::unique_ptr<cvd::NetworkInterfaceManager> nm(
      cvd::NetworkInterfaceManager::New(factory));
  std::unique_ptr<cvd::NetworkInterface> ni(nm->Open(data_name.c_str(),
                                                     eth_name));
  if (!ni) {
    ALOGE("open interface '%s' failed", eth_name);
    return 3;
  }
  ni->SetName(data_name.c_str());
  if (!nm->ApplyChanges(*ni)) {
    ALOGE("renaming interface '%s' failed", eth_name);
    return 4;
  }
  ni->SetOperational(true);
  if (!nm->ApplyChanges(*ni)) {
    ALOGE("unable to ifup '%s'", eth_name);
    return 5;
  }
  cvd::SharedFD bfd = cvd::SharedFD::Socket(AF_UNIX, SOCK_STREAM, 0);
  if (!bfd->IsOpen()) {
    ALOGE("unable to get socket (%s)", bfd->StrError());
    return 6;
  }
  std::string bridge_name(wifi_name);
  bridge_name += "_bridge";
  if (bfd->Ioctl(SIOCBRADDBR, (void*)bridge_name.c_str()) == -1) {
    ALOGE("unable create wlan0_bridge (%s)", bfd->StrError());
    return 7;
  }
  bridge_interface(bfd, bridge_name.c_str(), data_index, data_name.c_str());
  return 0;
}
