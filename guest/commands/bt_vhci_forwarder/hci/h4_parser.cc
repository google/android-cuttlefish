//
// Copyright 20 The Android Open Source Project
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

#include "hci/h4_parser.h"  // for H4Parser, PacketType, H4Pars...

#include <array>
#include <cstddef>     // for size_t
#include <cstdint>     // for uint8_t, int32_t
#include <functional>  // for function
#include <utility>     // for move
#include <vector>      // for vector

#include "log.h"  // for LOG_ALWAYS_FATAL, LOG_INFO

namespace rootcanal {

void H4Parser::Reset() {
  state_ = HCI_TYPE;
  packet_.clear();
  bytes_wanted_ = 0;
  packet_type_ = 0;
}

size_t H4Parser::HciGetPacketLengthForType(PacketType type,
                                           const uint8_t* preamble) {
  static const size_t
      packet_length_offset[static_cast<size_t>(PacketType::ISO) + 1] = {
          0,
          H4Parser::COMMAND_LENGTH_OFFSET,
          H4Parser::ACL_LENGTH_OFFSET,
          H4Parser::SCO_LENGTH_OFFSET,
          H4Parser::EVENT_LENGTH_OFFSET,
          H4Parser::ISO_LENGTH_OFFSET,
      };

  size_t offset = packet_length_offset[static_cast<size_t>(type)];
  size_t size = preamble[offset];
  if (type == PacketType::ACL) {
    size |= ((size_t)preamble[offset + 1]) << 8;
  }
  if (type == PacketType::ISO) {
    size |= ((size_t)preamble[offset + 1] & 0x0fU) << 8;
  }
  return size;
}

H4Parser::H4Parser(PacketReadCallback command_cb, PacketReadCallback event_cb,
                   PacketReadCallback acl_cb, PacketReadCallback sco_cb,
                   PacketReadCallback iso_cb, bool enable_recovery_state)
    : command_cb_(std::move(command_cb)),
      event_cb_(std::move(event_cb)),
      acl_cb_(std::move(acl_cb)),
      sco_cb_(std::move(sco_cb)),
      iso_cb_(std::move(iso_cb)),
      enable_recovery_state_(enable_recovery_state) {}

void H4Parser::OnPacketReady() {
  switch (hci_packet_type_) {
    case PacketType::COMMAND:
      command_cb_(packet_);
      break;
    case PacketType::ACL:
      acl_cb_(packet_);
      break;
    case PacketType::SCO:
      sco_cb_(packet_);
      break;
    case PacketType::EVENT:
      event_cb_(packet_);
      break;
    case PacketType::ISO:
      iso_cb_(packet_);
      break;
    default:
      LOG_ALWAYS_FATAL("Unimplemented packet type %d",
                       static_cast<int>(hci_packet_type_));
  }
  // Get ready for the next type byte.
  hci_packet_type_ = PacketType::UNKNOWN;
}

size_t H4Parser::BytesRequested() {
  switch (state_) {
    case HCI_TYPE:
    case HCI_RECOVERY:
      return 1;
    case HCI_PREAMBLE:
    case HCI_PAYLOAD:
      return bytes_wanted_;
  }
}

bool H4Parser::Consume(const uint8_t* buffer, int32_t bytes_read) {
  size_t bytes_to_read = BytesRequested();
  if (bytes_read <= 0) {
    LOG_INFO("remote disconnected, or unhandled error?");
    return false;
  }
  if ((uint32_t)bytes_read > BytesRequested()) {
    LOG_ALWAYS_FATAL("More bytes read (%u) than expected (%u)!",
                     static_cast<int>(bytes_read),
                     static_cast<int>(bytes_to_read));
  }

  static const size_t preamble_size[static_cast<size_t>(PacketType::ISO) + 1] =
      {
          0,
          H4Parser::COMMAND_PREAMBLE_SIZE,
          H4Parser::ACL_PREAMBLE_SIZE,
          H4Parser::SCO_PREAMBLE_SIZE,
          H4Parser::EVENT_PREAMBLE_SIZE,
          H4Parser::ISO_PREAMBLE_SIZE,
      };
  switch (state_) {
    case HCI_TYPE:
      // bytes_read >= 1
      packet_type_ = *buffer;
      packet_.clear();
      break;

    case HCI_RECOVERY: {
      // Skip all received bytes until the HCI Reset command is received.
      // The parser can end up in a bad state when the host is restarted.
      const std::array<uint8_t, 4> reset_command{0x01, 0x03, 0x0c, 0x00};
      size_t offset = packet_.size();
      LOG_WARN("Received byte in recovery state : 0x%x",
               static_cast<unsigned>(*buffer));
      packet_.push_back(*buffer);

      // Last byte does not match expected byte in the sequence.
      // Drop all the bytes and start over.
      if (packet_[offset] != reset_command[offset]) {
        packet_.clear();
        // The mismatched byte can also be the first of the correct sequence.
        if (*buffer == reset_command[0]) {
          packet_.push_back(*buffer);
        }
      }

      // Received full reset command.
      if (packet_.size() == reset_command.size()) {
        LOG_INFO("Received HCI Reset command, exiting recovery state");
        // Pop the Idc from the received packet.
        packet_.erase(packet_.begin());
        bytes_wanted_ = 0;
      }
      break;
    }

    case HCI_PREAMBLE:
    case HCI_PAYLOAD:
      packet_.insert(packet_.end(), buffer, buffer + bytes_read);
      bytes_wanted_ -= bytes_read;
      break;
  }

  switch (state_) {
    case HCI_TYPE:
      hci_packet_type_ = static_cast<PacketType>(packet_type_);
      if (hci_packet_type_ != PacketType::ACL &&
          hci_packet_type_ != PacketType::SCO &&
          hci_packet_type_ != PacketType::COMMAND &&
          hci_packet_type_ != PacketType::EVENT &&
          hci_packet_type_ != PacketType::ISO) {
        if (!enable_recovery_state_) {
          LOG_ALWAYS_FATAL("Received invalid packet type 0x%x",
                           static_cast<unsigned>(packet_type_));
        }
        LOG_ERROR("Received invalid packet type 0x%x, entering recovery state",
                  static_cast<unsigned>(packet_type_));
        state_ = HCI_RECOVERY;
        hci_packet_type_ = PacketType::COMMAND;
        bytes_wanted_ = 1;
      } else {
        state_ = HCI_PREAMBLE;
        bytes_wanted_ = preamble_size[static_cast<size_t>(hci_packet_type_)];
      }
      break;
    case HCI_PREAMBLE:
      if (bytes_wanted_ == 0) {
        size_t payload_size =
            HciGetPacketLengthForType(hci_packet_type_, packet_.data());
        if (payload_size == 0) {
          OnPacketReady();
          state_ = HCI_TYPE;
        } else {
          bytes_wanted_ = payload_size;
          state_ = HCI_PAYLOAD;
        }
      }
      break;
    case HCI_RECOVERY:
    case HCI_PAYLOAD:
      if (bytes_wanted_ == 0) {
        OnPacketReady();
        state_ = HCI_TYPE;
      }
      break;
  }
  return true;
}
}  // namespace rootcanal
