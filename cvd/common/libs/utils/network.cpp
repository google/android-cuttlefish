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
namespace {
// This should be the size of virtio_net_hdr_v1, from linux/virtio_net.h, but
// the version of that header that ships with android in Pie does not include
// that struct (it was added in Q).
// This is what that struct looks like:
// struct virtio_net_hdr_v1 {
// u8 flags;
// u8 gso_type;
// u16 hdr_len;
// u16 gso_size;
// u16 csum_start;
// u16 csum_offset;
// u16 num_buffers;
// };
static constexpr int SIZE_OF_VIRTIO_NET_HDR_V1 = 12;
}  // namespace

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

  // The interface's configuration may have been modified or just not set
  // correctly on creation. While qemu checks this and enforces the right
  // configuration, crosvm does not, so it needs to be set before it's passed to
  // it.
  tap_fd->Ioctl(TUNSETOFFLOAD,
                reinterpret_cast<void*>(TUN_F_CSUM | TUN_F_UFO | TUN_F_TSO4 |
                                        TUN_F_TSO6));
  int len = SIZE_OF_VIRTIO_NET_HDR_V1;
  tap_fd->Ioctl(TUNSETVNETHDRSZ, &len);

  return tap_fd;
}
}  // namespace cvd
