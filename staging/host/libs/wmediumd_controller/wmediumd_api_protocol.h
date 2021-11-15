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

#include <cstdint>
#include <memory>
#include <string>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

enum class WmediumdMessageType : uint32_t {
  kInvalid = 0,
  kAck = 1,
  kRegister = 2,
  kUnregister = 3,
  kNetlink = 4,
  kSetControl = 5,
  kTxStart = 6,
  kGetNodes = 7,
  kSetSnr = 8,
  kReloadConfig = 9,
  kReloadCurrentConfig = 10,
};

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

  uint8_t node1_mac_[6];
  uint8_t node2_mac_[6];
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

}  // namespace cuttlefish
