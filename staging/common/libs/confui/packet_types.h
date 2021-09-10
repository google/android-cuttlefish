//
// Copyright (C) 2021 The Android Open Source Project
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

#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace cuttlefish {
namespace confui {
namespace packet {
struct PayloadHeader {
  std::uint32_t payload_length_;
};

using BufferType = std::vector<std::uint8_t>;

// PayloadHeader + the byte size sent over the channel
using Payload = std::tuple<PayloadHeader, BufferType>;

// this is for short messages
constexpr const ssize_t kMaxPayloadLength = 10000;

using ConfUiPacketInfo = std::vector<std::vector<std::uint8_t>>;
struct ParsedPacket {
  std::string session_id_;
  std::string type_;
  ConfUiPacketInfo additional_info_;
};

std::string ToString(const ParsedPacket& packet);
}  // end of namespace packet
}  // end of namespace confui
}  // end of namespace cuttlefish
