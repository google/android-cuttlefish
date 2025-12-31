//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/metrics/utils.h"

#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <string>

#include "absl/log/log.h"

namespace cuttlefish::metrics {

static std::string Hashing(const std::string& input) {
  const std::hash<std::string> hasher;
  return std::to_string(hasher(input));
}

std::string GetOsName() {
  struct utsname buf;
  if (uname(&buf) != 0) {
    LOG(ERROR) << "failed to retrieve system information";
    return "Error";
  }
  return std::string(buf.sysname);
}

std::string GenerateSessionId(uint64_t now_ms) {
  uint64_t now_day = now_ms / 1000 / 60 / 60 / 24;
  return Hashing(GetMacAddress() + std::to_string(now_day));
}

std::string GetCfVersion() {
  // TODO: per ellisr@ leave empty for now
  return "";
}

std::string GetOsVersion() {
  struct utsname buf;
  if (uname(&buf) != 0) {
    LOG(ERROR) << "failed to retrieve system information";
  }
  std::string version = buf.release;
  return version;
}

std::string GetMacAddress() {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock == -1) {
    LOG(ERROR) << "couldn't connect to socket";
    return "";
  }

  char buf2[1024];
  struct ifconf ifc;
  ifc.ifc_len = sizeof(buf2);
  ifc.ifc_buf = buf2;
  if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) {
    LOG(ERROR) << "couldn't connect to socket";
    return "";
  }

  struct ifreq* it = ifc.ifc_req;
  const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));

  unsigned char mac_address[6] = {0};
  struct ifreq ifr;
  for (; it != end; ++it) {
    strncpy(ifr.ifr_name, it->ifr_name, strlen(it->ifr_name));
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) != 0) {
      LOG(ERROR) << "couldn't connect to socket";
      return "";
    }
    if (ifr.ifr_flags & IFF_LOOPBACK) {
      continue;
    }
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
      memcpy(mac_address, ifr.ifr_hwaddr.sa_data, 6);
      break;
    }
  }

  char mac[100];
  sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", mac_address[0], mac_address[1],
          mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
  return mac;
}

std::string GetCompany() {
  // TODO: per ellisr@ leave hard-coded for now
  return "GOOGLE";
}

std::string GetVmmVersion() {
  // TODO: per ellisr@ leave empty for now
  return "";
}

uint64_t GetEpochTimeMs() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  uint64_t milliseconds_since_epoch =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return milliseconds_since_epoch;
}

}  // namespace cuttlefish::metrics
