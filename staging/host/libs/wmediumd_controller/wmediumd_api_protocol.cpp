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
#include <iostream>
#include <string>
#include <vector>

#include "common/libs/fs/shared_buf.h"

template <class T>
static void AppendBinaryRepresentation(std::string& buf, const T& data) {
  std::copy(reinterpret_cast<const char*>(&data),
            reinterpret_cast<const char*>(&data) + sizeof(T),
            std::back_inserter(buf));
}

namespace cuttlefish {

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
  auto splitted_mac1 = android::base::Split(node1, ":");
  auto splitted_mac2 = android::base::Split(node2, ":");

  if (splitted_mac1.size() != 6) {
    LOG(FATAL) << "invalid mac address length " << node1;
  }

  if (splitted_mac2.size() != 6) {
    LOG(FATAL) << "invalid mac address length " << node2;
  }

  for (int i = 0; i < 6; i++) {
    char* end_ptr;
    node1_mac_[i] = (uint8_t)strtol(splitted_mac1[i].c_str(), &end_ptr, 16);
    if (end_ptr != splitted_mac1[i].c_str() + splitted_mac1[i].size()) {
      LOG(FATAL) << "cannot parse " << splitted_mac1[i] << " of " << node1;
    }

    node2_mac_[i] = (uint8_t)strtol(splitted_mac2[i].c_str(), &end_ptr, 16);
    if (end_ptr != splitted_mac2[i].c_str() + splitted_mac2[i].size()) {
      LOG(FATAL) << "cannot parse " << splitted_mac2[i] << " of " << node1;
    }
  }

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
    result.station_list_.push_back(
        *reinterpret_cast<const wmediumd_station_info*>(data + pos));
    pos += sizeof(wmediumd_station_info);
  }

  return result;
}

}  // namespace cuttlefish
