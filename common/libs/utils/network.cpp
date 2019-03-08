/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "common/libs/utils/network.h"

#include <linux/if.h>
#include <linux/if_tun.h>
#include <string.h>

#include "common/libs/glog/logging.h"

namespace cvd {
SharedFD OpenTapInterface(const std::string& interface_name) {
  constexpr auto TUNTAP_DEV = "/dev/net/tun";

  auto tap_fd = SharedFD::Open(TUNTAP_DEV, O_RDWR | O_NONBLOCK);
  if (!tap_fd->IsOpen()) {
    LOG(ERROR) << "Unable to open tun device: " << tap_fd->StrError();
    return tap_fd;
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_VNET_HDR;
  strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ);

  int err = tap_fd->Ioctl(TUNSETIFF, &ifr);
  if (err < 0) {
    LOG(ERROR) << "Unable to connect to " << interface_name
               << " tap interface: " << tap_fd->StrError();
    tap_fd->Close();
    return cvd::SharedFD();
  }

  return tap_fd;
}
}
