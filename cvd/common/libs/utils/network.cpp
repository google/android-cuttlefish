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

#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/types.h>
#include <linux/if_packet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ether.h>
#include <string.h>

#include <android-base/strings.h>
#include "android-base/logging.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/subprocess.h"

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

bool ParseAddress(const std::string& address, const std::string& separator,
                  const std::size_t expected_size, int base, std::uint8_t* out) {
  auto components = android::base::Split(address, separator);
  if (components.size() != expected_size) {
    LOG(ERROR) << "Address \"" << address << "\" had wrong number of parts. "
               << "Had " << components.size() << ", expected " << expected_size;
    return false;
  }
  for (int i = 0; i < expected_size; i++) {
    auto out_part = std::stoi(components[i], nullptr, base);
    if (out_part < 0 || out_part > 255) {
      LOG(ERROR) << "Address part " << i << " (" << out_part
                 << "): outside range [0,255]";
      return false;
    }
    out[i] = (std::uint8_t) out_part;
  }
  return true;
}

bool ParseMacAddress(const std::string& address, std::uint8_t mac[6]) {
  return ParseAddress(address, ":", 6, 16, mac);
}

bool ParseIpAddress(const std::string& address, std::uint8_t ip[4]) {
  return ParseAddress(address, ".", 4, 10, ip);
}

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

std::set<std::string> TapInterfacesInUse() {
  Command cmd("/bin/bash");
  cmd.AddParameter("-c");
  cmd.AddParameter("egrep -h -e \"^iff:.*\" /proc/*/fdinfo/*");
  std::string stdin, stdout, stderr;
  RunWithManagedStdio(std::move(cmd), &stdin, &stdout, &stderr);
  auto lines = android::base::Split(stdout, "\n");
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

std::vector<DnsmasqDhcp4Lease> ParseDnsmasqLeases(SharedFD lease_file) {
  std::string lease_file_content;
  if (ReadAll(lease_file, &lease_file_content) < 0) {
    LOG(ERROR) << "Could not read lease_file: \"" << lease_file->StrError()
               << "\". This may result in difficulty connecting to guest wifi.";
    return {};
  }
  std::vector<DnsmasqDhcp4Lease> leases;
  auto lease_file_lines = android::base::Split(lease_file_content, "\n");
  for (const auto& line : lease_file_lines) {
    if (line == "") {
      continue;
    }
    auto line_elements = android::base::Split(line, " ");
    if (line_elements.size() != 5) {
      LOG(WARNING) << "Could not parse lease line: \"" << line << "\"\n";
      continue;
    }
    DnsmasqDhcp4Lease lease;
    lease.expiry = std::stoll(line_elements[0]);
    if (!ParseMacAddress(line_elements[1], &lease.mac_address[0])) {
      LOG(WARNING) << "Could not parse MAC address: \'" << line_elements[1]
                   << "\"";
      continue;
    }
    if (!ParseIpAddress(line_elements[2], &lease.ip_address[0])) {
      LOG(WARNING) << "Could not parse IP address: " << line_elements[2]
                   << "\"";
    }
    lease.hostname = line_elements[3];
    lease.client_id = line_elements[4];
    leases.push_back(lease);
  }
  return leases;
}

std::ostream& operator<<(std::ostream& out, const DnsmasqDhcp4Lease& lease) {
  out << "DnsmasqDhcp4Lease(lease_time = \"" << std::dec << lease.expiry
      << ", mac_address = \"" << std::hex;
  for (int i = 0; i < 5; i++) {
    out << (int) lease.mac_address[i] << ":";
  }
  out << (int) lease.mac_address[5] << "\", ip_address = \"" << std::dec;
  for (int i = 0; i < 3; i++) {
    out << (int) lease.ip_address[i] << ".";
  }
  return out << (int) lease.ip_address[3] << "\", hostname = \""
             << lease.hostname << "\", client_id = \"" << lease.client_id
             << "\")";
}

struct __attribute__((packed)) Dhcp4MessageTypeOption {
  std::uint8_t code;
  std::uint8_t len;
  std::uint8_t message_type;
};

struct __attribute__((packed)) Dhcp4ServerIdentifier {
  std::uint8_t code;
  std::uint8_t len;
  std::uint8_t server_ip[4];
};

struct __attribute__((packed)) Dhcp4ReleaseMessage {
  std::uint8_t op;
  std::uint8_t htype;
  std::uint8_t hlen;
  std::uint8_t hops;
  __be32 xid;
  __be16 secs;
  __be16 flags;
  std::uint8_t client_ip[4];
  std::uint8_t assigned_ip[4];
  std::uint8_t server_ip[4];
  std::uint8_t gateway_ip[4];
  std::uint8_t client_harware_address[16];
  std::uint8_t server_name[64];
  std::uint8_t boot_filename[128];
  std::uint8_t magic_cookie[4];
  Dhcp4MessageTypeOption message_type;
  Dhcp4ServerIdentifier server_identifier;
  std::uint8_t end_code;
};

struct __attribute__((packed)) CompleteReleaseFrame {
  std::uint8_t vnet[SIZE_OF_VIRTIO_NET_HDR_V1];
  ether_header eth;
  iphdr ip;
  udphdr udp;
  Dhcp4ReleaseMessage dhcp;
};

static std::uint16_t ip_checksum(std::uint16_t *buf, std::size_t size) {
  std::uint32_t sum = 0;
  for (std::size_t i = 0; i < size; i++) {
    sum += buf[i];
  }
  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += sum >> 16;
  return (std::uint16_t) ~sum;
}

bool ReleaseDhcp4(SharedFD tap, const std::uint8_t mac_address[6],
                  const std::uint8_t ip_address[4],
                  const std::uint8_t dhcp_server_ip[4]) {
  CompleteReleaseFrame frame = {};
  *reinterpret_cast<std::uint16_t*>(&frame.vnet[2]) = // hdr_len, little-endian
      htole16(sizeof(ether_header) + sizeof(iphdr) + sizeof(udphdr));

  memcpy(frame.eth.ether_shost, mac_address, 6);
  memset(frame.eth.ether_dhost, 255, 6); // Broadcast
  frame.eth.ether_type = htobe16(ETH_P_IP);

  frame.ip.ihl = 5;
  frame.ip.version = 4;
  frame.ip.id = 0;
  frame.ip.ttl = 64; // hops
  frame.ip.protocol = 17; // UDP
  memcpy((std::uint8_t*) &frame.ip.saddr, ip_address, 4);
  frame.ip.daddr = *(std::uint32_t*) dhcp_server_ip;
  frame.ip.tot_len = htobe16(sizeof(frame.ip) + sizeof(frame.udp)
                             + sizeof(frame.dhcp));
  iphdr ip_copy = frame.ip; // original, it's in a packed struct
  frame.ip.check = ip_checksum((unsigned short*) &ip_copy,
                               sizeof(ip_copy) / sizeof(short));

  frame.udp.source = htobe16(68);
  frame.udp.dest = htobe16(67);
  frame.udp.len = htobe16(sizeof(frame.udp) + sizeof(frame.dhcp));

  frame.dhcp.op = 1; /* bootrequest */
  frame.dhcp.htype = 1; // Ethernet
  frame.dhcp.hlen = 6; /* mac address length */
  frame.dhcp.xid = rand();
  frame.dhcp.secs = htobe16(3);
  frame.dhcp.flags = 0;
  memcpy(frame.dhcp.client_ip, ip_address, 4);
  memcpy(frame.dhcp.client_harware_address, mac_address, 6);
  std::uint8_t magic_cookie[4] = {99, 130, 83, 99};
  memcpy(frame.dhcp.magic_cookie, magic_cookie, sizeof(magic_cookie));
  frame.dhcp.message_type = { .code = 53, .len = 1, .message_type = 7 };
  frame.dhcp.server_identifier.code = 54;
  frame.dhcp.server_identifier.len = 4;
  memcpy(frame.dhcp.server_identifier.server_ip, dhcp_server_ip, 4);
  frame.dhcp.end_code = 255;

  if (tap->Write((void*) &frame, sizeof(frame)) != sizeof(frame)) {
    LOG(ERROR) << "Could not write dhcprelease frame: \"" << tap->StrError()
               << "\". Connecting to wifi will likely not work.";
    return false;
  }
  return true;
}

}  // namespace cvd
