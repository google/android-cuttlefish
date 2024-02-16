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

#ifdef __linux__
#include <linux/if_ether.h>
// Kernel headers don't mix well with userspace headers, but there is no
// userspace header that provides the if_tun.h #defines.  Include the kernel
// header, but move conflicting definitions out of the way using macros.
#define ethhdr __kernel_ethhdr
#include <linux/if_tun.h>
#undef ethhdr
#endif

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <ios>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/subprocess.h"

namespace cuttlefish {
namespace {

#ifdef __linux__
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
#endif

/**
 * Generate mac address following:
 * 00:1a:11:e0:cf:index
 * ________ __    ______
 *    |      |          |
 *    |       type (e0, e1, etc)
*/
void GenerateMacForInstance(int index, uint8_t type, std::uint8_t out[6]) {
  // the first octet must be even
  out[0] = 0x00;
  out[1] = 0x1a;
  out[2] = 0x11;
  out[3] = type;
  out[4] = 0xcf;
  out[5] = static_cast<std::uint8_t>(index);
}

}  // namespace

bool NetworkInterfaceExists(const std::string& interface_name) {
  struct ifaddrs *ifa_list{}, *ifa{};
  bool ret = false;
  getifaddrs(&ifa_list);
  for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
    if (strcmp(ifa->ifa_name, interface_name.c_str()) == 0) {
      ret = true;
      break;
    }
  }
  freeifaddrs(ifa_list);
  return ret;
}

#ifdef __linux__
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
    return SharedFD();
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

std::set<std::string> TapInterfacesInUse() {
  Command cmd("/bin/bash");
  cmd.AddParameter("-c");
  cmd.AddParameter("egrep -h -e \"^iff:.*\" /proc/*/fdinfo/*");
  std::string stdin_str, stdout_str, stderr_str;
  RunWithManagedStdio(std::move(cmd), &stdin_str, &stdout_str, &stderr_str);
  auto lines = android::base::Split(stdout_str, "\n");
  std::set<std::string> tap_interfaces;
  for (const auto& line : lines) {
    if (line == "") {
      continue;
    }
    if (!android::base::StartsWith(line, "iff:\t")) {
      LOG(ERROR) << "Unexpected line \"" << line << "\"";
      continue;
    }
    tap_interfaces.insert(line.substr(std::string("iff:\t").size()));
  }
  return tap_interfaces;
}
#endif

std::string MacAddressToString(const std::uint8_t mac[6]) {
  std::vector<std::uint8_t> mac_vec(mac, mac + 6);
  return fmt::format("{:0>2x}", fmt::join(mac_vec, ":"));
}

std::string Ipv6ToString(const std::uint8_t ip[16]) {
  char ipv6_str[INET6_ADDRSTRLEN + 1];
  inet_ntop(AF_INET6, ip, ipv6_str, sizeof(ipv6_str));
  return std::string(ipv6_str);
}

void GenerateMobileMacForInstance(int index, std::uint8_t out[6]) {
  GenerateMacForInstance(index, 0xe0, out);
}

void GenerateEthMacForInstance(int index, std::uint8_t out[6]) {
  GenerateMacForInstance(index, 0xe1, out);
}

void GenerateWifiMacForInstance(int index, std::uint8_t out[6]) {
  GenerateMacForInstance(index, 0xe2, out);
}

/**
 * Linux uses mac to generate link-local IPv6 address following:
 *
 * 1. Get mac address (for example 00:1a:11:ee:cf:01)
 * 2. Throw ff:fe as a 3th and 4th octets (00:1a:11 :ff:fe: ee:cf:01)
 * 3. Flip 2th bit in the first octet (02: 1a:11:ff:fe:ee:cf:01)
 * 4. Use IPv6 format (021a:11ff:feee:cf01)
 * 5. Add prefix fe80:: (fe80::021a:11ff:feee:cf01 or fe80:0000:0000:0000:021a:11ff:feee:cf00)
*/
void GenerateCorrespondingIpv6ForMac(const std::uint8_t mac[6], std::uint8_t out[16]) {
  out[0] = 0xfe;
  out[1] = 0x80;

  // 2 - 7 octets are zero

  // need to invert 2th bit of the first octet
  out[8] = mac[0] ^ (1 << 1);
  out[9] = mac[1];

  out[10] = mac[2];
  out[11] = 0xff;

  out[12] = 0xfe;
  out[13] = mac[3];

  out[14] = mac[4];
  out[15] = mac[5];
}

}  // namespace cuttlefish
