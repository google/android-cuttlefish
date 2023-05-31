//
// Copyright 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include <stddef.h>  // for size_t

#include <cstdint>     // for uint8_t, int32_t
#include <functional>  // for function
#include <ostream>     // for operator<<, ostream
#include <vector>      // for vector

#include "hci/h4.h"  // for PacketType

namespace rootcanal {

using PacketReadCallback = std::function<void(const std::vector<uint8_t>&)>;
using HciPacketReadyCallback = std::function<void(void)>;
using ClientDisconnectCallback = std::function<void()>;

// An H4 Parser can parse H4 Packets and will invoke the proper callback
// once a packet has been parsed.
//
// You use it as follows:
//
// H4Parser h4(....);
// size_t nr_bytes = h4.BytesRequested();
// std::vector fill_this_vector_with_at_most_nr_bytes(nr_bytes);
// h4.Consume(fill_this_vector_with_at_most_nr_bytes.data(), nr_bytes.size());
//
// The parser will invoke the proper callbacks once a packet has been parsed.
// The parser keeps internal state and is not thread safe.
class H4Parser {
 public:
  enum State { HCI_TYPE, HCI_PREAMBLE, HCI_PAYLOAD, HCI_RECOVERY };

  H4Parser(PacketReadCallback command_cb, PacketReadCallback event_cb,
           PacketReadCallback acl_cb, PacketReadCallback sco_cb,
           PacketReadCallback iso_cb, bool enable_recovery_state = false);

  // Consumes the given number of bytes, returns true on success.
  bool Consume(const uint8_t* buffer, int32_t bytes);

  // The maximum number of bytes the parser can consume in the current state.
  size_t BytesRequested();

  // Resets the parser to the empty, initial state.
  void Reset();

  State CurrentState() { return state_; };

  void EnableRecovery() { enable_recovery_state_ = true; }
  void DisableRecovery() { enable_recovery_state_ = false; }

 private:
  void OnPacketReady();

  // 2 bytes for opcode, 1 byte for parameter length (Volume 2, Part E, 5.4.1)
  static constexpr size_t COMMAND_PREAMBLE_SIZE = 3;
  static constexpr size_t COMMAND_LENGTH_OFFSET = 2;
  // 2 bytes for handle, 2 bytes for data length (Volume 2, Part E, 5.4.2)
  static constexpr size_t ACL_PREAMBLE_SIZE = 4;
  static constexpr size_t ACL_LENGTH_OFFSET = 2;

  // 2 bytes for handle, 1 byte for data length (Volume 2, Part E, 5.4.3)
  static constexpr size_t SCO_PREAMBLE_SIZE = 3;
  static constexpr size_t SCO_LENGTH_OFFSET = 2;

  // 1 byte for event code, 1 byte for parameter length (Volume 2, Part
  // E, 5.4.4)
  static constexpr size_t EVENT_PREAMBLE_SIZE = 2;
  static constexpr size_t EVENT_LENGTH_OFFSET = 1;

  // 2 bytes for handle and flags, 12 bits for length (Volume 2, Part E, 5.4.5)
  static constexpr size_t ISO_PREAMBLE_SIZE = 4;
  static constexpr size_t ISO_LENGTH_OFFSET = 2;

  PacketReadCallback command_cb_;
  PacketReadCallback event_cb_;
  PacketReadCallback acl_cb_;
  PacketReadCallback sco_cb_;
  PacketReadCallback iso_cb_;

  static size_t HciGetPacketLengthForType(PacketType type,
                                          const uint8_t* preamble);

  PacketType hci_packet_type_{PacketType::UNKNOWN};

  State state_{HCI_TYPE};
  uint8_t packet_type_{};
  std::vector<uint8_t> packet_;
  size_t bytes_wanted_{0};
  bool enable_recovery_state_{false};
};

inline std::ostream& operator<<(std::ostream& os,
                                H4Parser::State const& state_) {
  switch (state_) {
    case H4Parser::State::HCI_TYPE:
      os << "HCI_TYPE";
      break;
    case H4Parser::State::HCI_PREAMBLE:
      os << "HCI_PREAMBLE";
      break;
    case H4Parser::State::HCI_PAYLOAD:
      os << "HCI_PAYLOAD";
      break;
    case H4Parser::State::HCI_RECOVERY:
      os << "HCI_RECOVERY";
      break;
    default:
      os << "unknown state " << static_cast<int>(state_);
      break;
  }
  return os;
}

}  // namespace rootcanal
