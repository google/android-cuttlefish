/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "wmediumd_api_protocol.h"

#include <android-base/logging.h>
#include <android-base/strings.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "common/libs/fs/shared_buf.h"

#define MAC_ADDR_LEN 6
#define STR_MAC_ADDR_LEN 17

template <class T>
static void AppendBinaryRepresentation(std::string& buf, const T& data) {
  std::copy(reinterpret_cast<const char*>(&data),
            reinterpret_cast<const char*>(&data) + sizeof(T),
            std::back_inserter(buf));
}

static std::array<uint8_t, 6> ParseMacAddress(const std::string& macAddr) {
  if (!cuttlefish::ValidMacAddr(macAddr)) {
    LOG(FATAL) << "invalid mac address " << macAddr;
  }

  auto split_mac = android::base::Split(macAddr, ":");
  std::array<uint8_t, 6> mac;
  for (int i = 0; i < 6; i++) {
    char* end_ptr;
    mac[i] = (uint8_t)strtol(split_mac[i].c_str(), &end_ptr, 16);
  }

  return mac;
}

namespace cuttlefish {

bool ValidMacAddr(const std::string& macAddr) {
  if (macAddr.size() != STR_MAC_ADDR_LEN) {
    return false;
  }

  if (macAddr[2] != ':' || macAddr[5] != ':' || macAddr[8] != ':' ||
      macAddr[11] != ':' || macAddr[14] != ':') {
    return false;
  }

  for (int i = 0; i < STR_MAC_ADDR_LEN; ++i) {
    if ((i - 2) % 3 == 0) continue;
    char c = macAddr[i];

    if (isupper(c)) {
      c = tolower(c);
    }

    if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) return false;
  }

  return true;
}

std::string MacToString(const char* macAddr) {
  std::stringstream result;

  for (int i = 0; i < MAC_ADDR_LEN; i++) {
    result << std::setfill('0') << std::setw(2) << std::right << std::hex
           << static_cast<int>(static_cast<uint8_t>(macAddr[i]));

    if (i != 5) {
      result << ":";
    }
  }

  return result.str();
}

std::string WmediumdMessage::Serialize(void) const {
  std::string result;

  AppendBinaryRepresentation(result, this->Type());

  std::string body;
  this->SerializeBody(body);

  AppendBinaryRepresentation(result, static_cast<uint32_t>(body.size()));

  std::copy(std::begin(body), std::end(body), std::back_inserter(result));

  return result;
}

void WmediumdMessageSetControl::SerializeBody(std::string& buf) const {
  AppendBinaryRepresentation(buf, flags_);
}

WmediumdMessageSetSnr::WmediumdMessageSetSnr(const std::string& node1,
                                             const std::string& node2,
                                             uint8_t snr) {
  node1_mac_ = ParseMacAddress(node1);
  node2_mac_ = ParseMacAddress(node2);
  snr_ = snr;
}

void WmediumdMessageSetSnr::SerializeBody(std::string& buf) const {
  std::copy(std::begin(node1_mac_), std::end(node1_mac_),
            std::back_inserter(buf));
  std::copy(std::begin(node2_mac_), std::end(node2_mac_),
            std::back_inserter(buf));
  buf.push_back(snr_);
}

void WmediumdMessageReloadConfig::SerializeBody(std::string& buf) const {
  std::copy(std::begin(config_path_), std::end(config_path_),
            std::back_inserter(buf));
  buf.push_back('\0');
}

void WmediumdMessageStartPcap::SerializeBody(std::string& buf) const {
  std::copy(std::begin(pcap_path_), std::end(pcap_path_),
            std::back_inserter(buf));
  buf.push_back('\0');
}

WmediumdMessageSetPosition::WmediumdMessageSetPosition(const std::string& node,
                                                       double x, double y) {
  mac_ = ParseMacAddress(node);
  x_ = x;
  y_ = y;
}

void WmediumdMessageSetPosition::SerializeBody(std::string& buf) const {
  std::copy(std::begin(mac_), std::end(mac_), std::back_inserter(buf));
  AppendBinaryRepresentation(buf, x_);
  AppendBinaryRepresentation(buf, y_);
}

WmediumdMessageSetLci::WmediumdMessageSetLci(const std::string& node,
                                             const std::string& lci) {
  mac_ = ParseMacAddress(node);
  lci_ = lci;
}

void WmediumdMessageSetLci::SerializeBody(std::string& buf) const {
  std::copy(std::begin(mac_), std::end(mac_), std::back_inserter(buf));
  std::copy(std::begin(lci_), std::end(lci_), std::back_inserter(buf));
  buf.push_back('\0');
}

WmediumdMessageSetCivicloc::WmediumdMessageSetCivicloc(
    const std::string& node, const std::string& civicloc) {
  mac_ = ParseMacAddress(node);
  civicloc_ = civicloc;
}

void WmediumdMessageSetCivicloc::SerializeBody(std::string& buf) const {
  std::copy(std::begin(mac_), std::end(mac_), std::back_inserter(buf));
  std::copy(std::begin(civicloc_), std::end(civicloc_),
            std::back_inserter(buf));
  buf.push_back('\0');
}

std::optional<WmediumdMessageStationsList> WmediumdMessageStationsList::Parse(
    const WmediumdMessageReply& reply) {
  size_t pos = 0;
  size_t dataSize = reply.Size();
  auto data = reply.Data();

  if (reply.Type() != WmediumdMessageType::kStationsList) {
    LOG(FATAL) << "expected reply type "
               << static_cast<uint32_t>(WmediumdMessageType::kStationsList)
               << ", got " << static_cast<uint32_t>(reply.Type()) << std::endl;
  }

  WmediumdMessageStationsList result;

  if (pos + sizeof(uint32_t) > dataSize) {
    LOG(ERROR) << "invalid response size";
    return std::nullopt;
  }

  uint32_t count = *reinterpret_cast<const uint32_t*>(data + pos);
  pos += sizeof(uint32_t);

  for (uint32_t i = 0; i < count; ++i) {
    if (pos + sizeof(wmediumd_station_info) > dataSize) {
      LOG(ERROR) << "invalid response size";
      return std::nullopt;
    }

    const wmediumd_station_info* station =
        reinterpret_cast<const wmediumd_station_info*>(data + pos);
    std::string lci((char*)station + station->lci_offset);
    std::string civicloc((char*)station + station->civicloc_offset);
    result.station_list_.emplace_back(station->addr, station->hwaddr,
                                      station->x, station->y, lci, civicloc,
                                      station->tx_power);
    pos += sizeof(wmediumd_station_info);
  }

  return result;
}

}  // namespace cuttlefish
