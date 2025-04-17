/*
 * Copyright 2023 The Android Open Source Project
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

#include <fcntl.h>
#include <chrono>
#include <cstdint>

#include "host/commands/casimir_control_server/crc.h"

#include "casimir_control.grpc.pb.h"
#include "casimir_controller.h"

namespace cuttlefish {

using namespace casimir::rf;
using namespace std::literals::chrono_literals;
using pdl::packet::slice;

Result<void> CasimirController::Mute() {
  if (!sock_->IsOpen()) {
    return {};
  }
  FieldInfoBuilder rf_off;
  rf_off.field_status_ = FieldStatus::FieldOff;
  rf_off.power_level_ = power_level;
  CF_EXPECT(Write(rf_off));
  return {};
}

CasimirController::CasimirController(SharedFD sock)
    : sock_(sock), power_level(10) {}

/* static */
Result<CasimirController> CasimirController::ConnectToTcpPort(int rf_port) {
  SharedFD sock = SharedFD::SocketLocalClient(rf_port, SOCK_STREAM);
  CF_EXPECT(sock->IsOpen(),
            "Failed to connect to casimir with RF port" << rf_port);

  int flags = sock->Fcntl(F_GETFL, 0);
  CF_EXPECT_GE(flags, 0, "Failed to get FD flags of casimir socket");
  CF_EXPECT_EQ(sock->Fcntl(F_SETFL, flags | O_NONBLOCK), 0,
               "Failed to set casimir socket nonblocking");

  return CasimirController(sock);
}

/* static */
Result<CasimirController> CasimirController::ConnectToUnixSocket(
    const std::string& rf_path) {
  SharedFD sock = SharedFD::SocketLocalClient(rf_path, false, SOCK_STREAM);
  CF_EXPECT(sock->IsOpen(),
            "Failed to connect to casimir with RF path" << rf_path);

  int flags = sock->Fcntl(F_GETFL, 0);
  CF_EXPECT_GE(flags, 0, "Failed to get FD flags of casimir socket");
  CF_EXPECT_EQ(sock->Fcntl(F_SETFL, flags | O_NONBLOCK), 0,
               "Failed to set casimir socket nonblocking");
  return CasimirController(sock);
}

Result<void> CasimirController::Unmute() {
  if (!sock_->IsOpen()) {
    return {};
  }
  FieldInfoBuilder rf_on;
  rf_on.field_status_ = FieldStatus::FieldOn;
  rf_on.power_level_ = power_level;
  CF_EXPECT(Write(rf_on));
  return {};
}

Result<void> CasimirController::SetPowerLevel(uint32_t power_level) {
  this->power_level = power_level;
  return {};
}

Result<uint16_t> CasimirController::SelectNfcA() {
  PollCommandBuilder poll_command;
  poll_command.technology_ = Technology::NFC_A;
  poll_command.format_ = PollingFrameFormat::SHORT;
  poll_command.bitrate_ = BitRate::BIT_RATE_106_KBIT_S;
  poll_command.power_level_ = power_level;
  // WUPA
  poll_command.payload_ = std::vector<uint8_t>{0x52};
  CF_EXPECT(Write(poll_command), "Failed to send NFC-A poll command");

  auto res = CF_EXPECT(ReadRfPacket(10s), "Failed to get NFC-A poll response");

  auto rf_packet = RfPacketView::Create(slice(res));
  if (rf_packet.IsValid()) {
    auto poll_response = NfcAPollResponseView::Create(rf_packet);
    if (poll_response.IsValid() && poll_response.GetIntProtocol() == 0b01) {
      return poll_response.GetSender();
    }
  }
  return CF_ERR("Invalid Poll-A response");
}

Result<void> CasimirController::SelectT4AT(uint16_t sender_id) {
  T4ATSelectCommandBuilder t4at_select_command;
  t4at_select_command.sender_ = sender_id;
  t4at_select_command.param_ = 0;
  t4at_select_command.bitrate_ = BitRate::BIT_RATE_106_KBIT_S;
  CF_EXPECT(Write(t4at_select_command), "Failed to send T4AT select command");

  auto res = CF_EXPECT(ReadRfPacket(1s), "Failed to get T4AT response");

  // Note: T4AT select response implies NFC_A and ISO_DEP
  auto rf_packet = RfPacketView::Create(slice(res));
  if (rf_packet.IsValid()) {
    auto select_response = T4ATSelectResponseView::Create(rf_packet);
    if (select_response.IsValid() && select_response.GetSender() == sender_id) {
      return {};
    }
  }
  return CF_ERR("Invalid T4AT response");
}

Result<uint16_t> CasimirController::Poll() {
  CF_EXPECT(sock_->IsOpen());

  uint16_t sender_id = CF_EXPECT(SelectNfcA(), "Failed to select NFC-A");
  CF_EXPECT(SelectT4AT(sender_id), "Failed to select T4AT");
  return sender_id;
}

Result<std::vector<uint8_t>> CasimirController::SendApdu(
    uint16_t receiver_id, std::vector<uint8_t> apdu) {
  CF_EXPECT(sock_->IsOpen());

  DataBuilder data_builder;
  data_builder.data_ = std::move(apdu);
  data_builder.receiver_ = receiver_id;
  data_builder.technology_ = Technology::NFC_A;
  data_builder.protocol_ = Protocol::ISO_DEP;
  data_builder.bitrate_ = BitRate::BIT_RATE_106_KBIT_S;

  CF_EXPECT(Write(data_builder), "Failed to send APDU bytes");

  auto res = CF_EXPECT(ReadRfPacket(3s), "Failed to get APDU response");
  auto rf_packet = RfPacketView::Create(slice(res));
  if (rf_packet.IsValid()) {
    auto data = DataView::Create(rf_packet);
    if (data.IsValid() && rf_packet.GetSender() == receiver_id) {
      return data.GetData();
    }
  }
  return CF_ERR("Invalid APDU response");
}

Result<std::tuple<std::vector<uint8_t>, std::string, bool, uint32_t, uint32_t,
                  uint32_t, double>>
CasimirController::SendBroadcast(std::vector<uint8_t> data, std::string type,
                                 bool crc, uint8_t bits, uint32_t bitrate,
                                 uint32_t timeout, double power) {
  PollCommandBuilder poll_command;

  if (type == "A") {
    poll_command.technology_ = Technology::NFC_A;
    if (crc) {
      data = CF_EXPECT(WithCrc16A(data), "Could not append CRC16A");
    }
  } else if (type == "B") {
    poll_command.technology_ = Technology::NFC_B;
    if (crc) {
      data = CF_EXPECT(WithCrc16B(data), "Could not append CRC16B");
    }
    if (bits != 8) {
      return CF_ERR(
          "Sending NFC-B data with != 8 bits in the last byte is unsupported");
    }
  } else if (type == "F") {
    poll_command.technology_ = Technology::NFC_F;
    if (!crc) {
      // For NFC-F, CRC also assumes preamble
      return CF_ERR("Sending NFC-F data without CRC is unsupported");
    }
    if (bits != 8) {
      return CF_ERR(
          "Sending NFC-F data with != 8 bits in the last byte is unsupported");
    }
  } else if (type == "V") {
    poll_command.technology_ = Technology::NFC_V;
  } else {
    poll_command.technology_ = Technology::RAW;
  }

  if (bitrate == 106) {
    poll_command.bitrate_ = BitRate::BIT_RATE_106_KBIT_S;
  } else if (bitrate == 212) {
    poll_command.bitrate_ = BitRate::BIT_RATE_212_KBIT_S;
  } else if (bitrate == 424) {
    poll_command.bitrate_ = BitRate::BIT_RATE_424_KBIT_S;
  } else if (bitrate == 848) {
    poll_command.bitrate_ = BitRate::BIT_RATE_848_KBIT_S;
  } else if (bitrate == 1695) {
    poll_command.bitrate_ = BitRate::BIT_RATE_1695_KBIT_S;
  } else if (bitrate == 3390) {
    poll_command.bitrate_ = BitRate::BIT_RATE_3390_KBIT_S;
  } else if (bitrate == 6780) {
    poll_command.bitrate_ = BitRate::BIT_RATE_6780_KBIT_S;
  } else if (bitrate == 26) {
    poll_command.bitrate_ = BitRate::BIT_RATE_26_KBIT_S;
  } else {
    return CF_ERR("Proper bitrate was not provided: " << bitrate);
  }

  poll_command.payload_ = std::move(data);

  if (bits > 8) {
    return CF_ERR("There can not be more than 8 bits in last byte: " << bits);
  }
  poll_command.format_ =
      bits != 8 ? PollingFrameFormat::SHORT : PollingFrameFormat::LONG;

  // Adjust range of values from 0-100 to 0-12
  poll_command.power_level_ = static_cast<int>(std::round(power * 12 / 100));

  CF_EXPECT(Write(poll_command), "Failed to send broadcast frame");

  if (timeout != 0) {
    ReadRfPacket(std::chrono::microseconds(timeout));
  }

  return std::make_tuple(data, type, crc, bits, bitrate, timeout, power);
}

Result<void> CasimirController::Write(const RfPacketBuilder& rf_packet) {
  std::vector<uint8_t> raw_bytes = rf_packet.SerializeToBytes();
  uint16_t header_bytes_le = htole16(raw_bytes.size());
  ssize_t written = WriteAll(sock_, reinterpret_cast<char*>(&header_bytes_le),
                             sizeof(header_bytes_le));
  CF_EXPECT_EQ(written, sizeof(header_bytes_le),
               "Failed to write packet header to casimir socket, errno="
                   << sock_->GetErrno());

  written = WriteAll(sock_, reinterpret_cast<char*>(raw_bytes.data()),
                     raw_bytes.size());
  CF_EXPECT_EQ(written, raw_bytes.size(),
               "Failed to write packet payload to casimir socket, errno="
                   << sock_->GetErrno());

  return {};
}

Result<std::shared_ptr<std::vector<uint8_t>>> CasimirController::ReadExact(
    size_t size, std::chrono::microseconds timeout) {
  size_t total_read = 0;
  auto out = std::make_shared<std::vector<uint8_t>>(size);
  auto prev_time = std::chrono::steady_clock::now();
  while (
      std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count() >
      0) {
    PollSharedFd poll_fd = {
        .fd = sock_,
        .events = EPOLLIN,
        .revents = 0,
    };
    int res = sock_.Poll(
        &poll_fd, 1,
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    CF_EXPECT_GE(res, 0, "Failed to poll on the casimir socket");
    CF_EXPECT_EQ(poll_fd.revents, EPOLLIN,
                 "Unexpected poll result for reading");

    // Nonblocking read, so don't need to care about timeout.
    ssize_t read =
        sock_->Read((void*)&(out->data()[total_read]), size - total_read);
    CF_EXPECT_GT(
        read, 0,
        "Failed to read from casimir socket, errno=" << sock_->GetErrno());

    total_read += read;
    if (total_read >= size) {
      return out;
    }

    auto current_time = std::chrono::steady_clock::now();
    timeout -= std::chrono::duration_cast<std::chrono::microseconds>(
        current_time - prev_time);
  }

  return CF_ERR("Failed to read from casimir socket; timed out");
}

// Note: Although rf_packets.h doesn't document nor include packet header,
// the header is necessary to know total packet size.
Result<std::shared_ptr<std::vector<uint8_t>>> CasimirController::ReadRfPacket(
    std::chrono::microseconds timeout) {
  auto start_time = std::chrono::steady_clock::now();

  auto res = CF_EXPECT(ReadExact(sizeof(uint16_t), timeout),
                       "Failed to read RF packet header");
  slice packet_size_slice(res);
  int16_t packet_size = packet_size_slice.read_le<uint16_t>();

  auto current_time = std::chrono::steady_clock::now();
  timeout -= std::chrono::duration_cast<std::chrono::microseconds>(
      current_time - start_time);
  return CF_EXPECT(ReadExact(packet_size, timeout),
                   "Failed to read RF packet payload");
}

}  // namespace cuttlefish
