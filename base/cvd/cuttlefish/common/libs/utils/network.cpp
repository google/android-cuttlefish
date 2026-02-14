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

#include "cuttlefish/common/libs/utils/network.h"

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
#include <stdint.h>

#include <cstring>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <fmt/ranges.h>
#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

/**
 * Generate mac address following:
 * 00:1a:11:e0:cf:index
 * ________ __    ______
 *    |      |          |
 *    |       type (e0, e1, etc)
*/
void GenerateMacForInstance(int index, uint8_t type, uint8_t out[6]) {
  // the first octet must be even
  out[0] = 0x00;
  out[1] = 0x1a;
  out[2] = 0x11;
  out[3] = type;
  out[4] = 0xcf;
  out[5] = static_cast<uint8_t>(index);
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
static std::optional<Command> GrepCommand() {
  if (FileExists("/usr/bin/grep")) {
    return Command("/usr/bin/grep");
  } else if (FileExists("/bin/grep")) {
    return Command("/bin/grep");
  } else {
    return {};
  }
}

std::set<std::string> TapInterfacesInUse() {
  std::vector<std::string> fdinfo_list;

  Result<std::vector<std::string>> processes = DirectoryContents("/proc");
  if (!processes.ok()) {
    LOG(ERROR) << "Failed to get contents of `/proc/`";
    return {};
  }
  for (const std::string& process : *processes) {
    std::string fdinfo_path = fmt::format("/proc/{}/fdinfo", process);
    Result<std::vector<std::string>> fdinfos = DirectoryContents(fdinfo_path);
    if (!fdinfos.ok()) {
      VLOG(1) << "Failed to get contents of '" << fdinfo_path << "'";
      continue;
    }
    for (const std::string& fdinfo : *fdinfos) {
      std::string path = fmt::format("/proc/{}/fdinfo/{}", process, fdinfo);
      fdinfo_list.emplace_back(std::move(path));
    }
  }

  std::optional<Command> cmd = GrepCommand();
  if (!cmd) {
    LOG(WARNING) << "Unable to test TAP interface usage";
    return {};
  }
  cmd->AddParameter("-E").AddParameter("-h").AddParameter("-e").AddParameter(
      "^iff:.*");

  for (const std::string& fdinfo : fdinfo_list) {
    cmd->AddParameter(fdinfo);
  }

  std::string stdout_str = RunAndCaptureStdout(std::move(*cmd)).value_or("");

  auto lines = android::base::Split(stdout_str, "\n");
  std::set<std::string> tap_interfaces;
  for (const auto& line : lines) {
    if (line.empty()) {
      continue;
    }
    if (!absl::StartsWith(line, "iff:\t")) {
      LOG(ERROR) << "Unexpected line \"" << line << "\"";
      continue;
    }
    tap_interfaces.insert(line.substr(std::string("iff:\t").size()));
  }
  return tap_interfaces;
}
#endif

std::string MacAddressToString(const uint8_t mac[6]) {
  std::vector<uint8_t> mac_vec(mac, mac + 6);
  return fmt::format("{:0>2x}", fmt::join(mac_vec, ":"));
}

std::string Ipv6ToString(const uint8_t ip[16]) {
  char ipv6_str[INET6_ADDRSTRLEN + 1];
  inet_ntop(AF_INET6, ip, ipv6_str, sizeof(ipv6_str));
  return std::string(ipv6_str);
}

void GenerateMobileMacForInstance(int index, uint8_t out[6]) {
  GenerateMacForInstance(index, 0xe0, out);
}

void GenerateEthMacForInstance(int index, uint8_t out[6]) {
  GenerateMacForInstance(index, 0xe1, out);
}

void GenerateWifiMacForInstance(int index, uint8_t out[6]) {
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
void GenerateCorrespondingIpv6ForMac(const uint8_t mac[6], uint8_t out[16]) {
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
