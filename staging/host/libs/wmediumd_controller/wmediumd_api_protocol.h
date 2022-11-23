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

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <wmediumd/api.h>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

enum class WmediumdMessageType : uint32_t {
  kInvalid = WMEDIUMD_MSG_INVALID,
  kAck = WMEDIUMD_MSG_ACK,
  kRegister = WMEDIUMD_MSG_REGISTER,
  kUnregister = WMEDIUMD_MSG_UNREGISTER,
  kNetlink = WMEDIUMD_MSG_NETLINK,
  kSetControl = WMEDIUMD_MSG_SET_CONTROL,
  kTxStart = WMEDIUMD_MSG_TX_START,
  kGetStations = WMEDIUMD_MSG_GET_STATIONS,
  kSetSnr = WMEDIUMD_MSG_SET_SNR,
  kReloadConfig = WMEDIUMD_MSG_RELOAD_CONFIG,
  kReloadCurrentConfig = WMEDIUMD_MSG_RELOAD_CURRENT_CONFIG,
  kStartPcap = WMEDIUMD_MSG_START_PCAP,
  kStopPcap = WMEDIUMD_MSG_STOP_PCAP,
  kStationsList = WMEDIUMD_MSG_STATIONS_LIST,
  kSetPosition = WMEDIUMD_MSG_SET_POSITION,
  kSetLci = WMEDIUMD_MSG_SET_LCI,
  kSetCivicloc = WMEDIUMD_MSG_SET_CIVICLOC,
};

struct WmediumdStationInfo {
  char addr[ETH_ALEN];
  char hwaddr[ETH_ALEN];

  double x;
  double y;

  std::string lci;
  std::string civicloc;

  int tx_power;

  WmediumdStationInfo(const char addr[ETH_ALEN], const char hwaddr[ETH_ALEN],
                      double x, double y, const std::string& lci,
                      const std::string& civicloc, int tx_power)
      : x(x), y(y), lci(lci), civicloc(civicloc), tx_power(tx_power) {
    memcpy(this->addr, addr, sizeof(this->addr));
    memcpy(this->hwaddr, hwaddr, sizeof(this->hwaddr));
  }
};

bool ValidMacAddr(const std::string& macAddr);
std::string MacToString(const char* macAddr);

class WmediumdMessage {
 public:
  virtual ~WmediumdMessage() {}

  std::string Serialize(void) const;

  virtual WmediumdMessageType Type() const = 0;

 private:
  virtual void SerializeBody(std::string&) const {};
};

class WmediumdMessageSetControl : public WmediumdMessage {
 public:
  WmediumdMessageSetControl(uint32_t flags) : flags_(flags) {}

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kSetControl;
  }

 private:
  void SerializeBody(std::string& out) const override;
  uint32_t flags_;
};

class WmediumdMessageSetSnr : public WmediumdMessage {
 public:
  WmediumdMessageSetSnr(const std::string& node1, const std::string& node2,
                        uint8_t snr);

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kSetSnr;
  }

 private:
  void SerializeBody(std::string& out) const override;

  std::array<uint8_t, 6> node1_mac_;
  std::array<uint8_t, 6> node2_mac_;
  uint8_t snr_;
};

class WmediumdMessageReloadConfig : public WmediumdMessage {
 public:
  WmediumdMessageReloadConfig(const std::string& configPath)
      : config_path_(configPath) {}

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kReloadConfig;
  }

 private:
  void SerializeBody(std::string& out) const override;

  std::string config_path_;
};

class WmediumdMessageReloadCurrentConfig : public WmediumdMessage {
 public:
  WmediumdMessageReloadCurrentConfig() = default;

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kReloadCurrentConfig;
  }
};

class WmediumdMessageStartPcap : public WmediumdMessage {
 public:
  WmediumdMessageStartPcap(const std::string& pcapPath)
      : pcap_path_(pcapPath) {}

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kStartPcap;
  }

 private:
  void SerializeBody(std::string& out) const override;

  std::string pcap_path_;
};

class WmediumdMessageStopPcap : public WmediumdMessage {
 public:
  WmediumdMessageStopPcap() = default;

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kStopPcap;
  }
};

class WmediumdMessageGetStations : public WmediumdMessage {
 public:
  WmediumdMessageGetStations() = default;

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kGetStations;
  }
};

class WmediumdMessageReply : public WmediumdMessage {
 public:
  WmediumdMessageReply() = default;
  WmediumdMessageReply(WmediumdMessageType type, const std::string& data)
      : type_(type), data_(data) {}

  WmediumdMessageType Type() const override { return type_; }

  size_t Size() const { return data_.size(); }
  const char* Data() const { return data_.data(); }

 private:
  WmediumdMessageType type_;
  std::string data_;
};

class WmediumdMessageStationsList : public WmediumdMessage {
 public:
  WmediumdMessageStationsList() = default;
  static std::optional<WmediumdMessageStationsList> Parse(
      const WmediumdMessageReply& reply);

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kStationsList;
  }

  const std::vector<WmediumdStationInfo>& GetStations() const {
    return station_list_;
  }

 private:
  std::vector<WmediumdStationInfo> station_list_;
};

class WmediumdMessageSetPosition : public WmediumdMessage {
 public:
  WmediumdMessageSetPosition(const std::string& node, double x, double y);

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kSetPosition;
  }

 private:
  void SerializeBody(std::string& out) const override;

  std::array<uint8_t, 6> mac_;
  double x_;
  double y_;
};

class WmediumdMessageSetLci : public WmediumdMessage {
 public:
  WmediumdMessageSetLci(const std::string& node, const std::string& lci);

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kSetLci;
  }

 private:
  void SerializeBody(std::string& out) const override;

  std::array<uint8_t, 6> mac_;
  std::string lci_;
};

class WmediumdMessageSetCivicloc : public WmediumdMessage {
 public:
  WmediumdMessageSetCivicloc(const std::string& node,
                             const std::string& civicloc);

  WmediumdMessageType Type() const override {
    return WmediumdMessageType::kSetCivicloc;
  }

 private:
  void SerializeBody(std::string& out) const override;

  std::array<uint8_t, 6> mac_;
  std::string civicloc_;
};

}  // namespace cuttlefish
