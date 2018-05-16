/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <utility>
#include <vector>
#include <memory>

#include "common/vsoc/lib/typed_region_view.h"
#include "common/vsoc/shm/socket_forward_layout.h"

namespace vsoc {
namespace socket_forward {

struct Header {
  std::uint32_t payload_length;
  std::uint32_t generation;
  enum MessageType : std::uint32_t {
    DATA = 0,
    BEGIN,
    END,
  };
  MessageType message_type;
};

constexpr std::size_t kMaxPayloadSize =
    layout::socket_forward::kMaxPacketSize - sizeof(Header);

struct Packet {
 private:
  Header header_;
  using Payload = char[kMaxPayloadSize];
  Payload payload_data_;

  static Packet MakePacket(Header::MessageType type) {
    Packet packet{};
    packet.header_.message_type = type;
    return packet;
  }

 public:
  static Packet MakeBegin() { return MakePacket(Header::BEGIN); }

  static Packet MakeEnd() { return MakePacket(Header::END); }

  // NOTE payload and payload_length must still be set.
  static Packet MakeData() { return MakePacket(Header::DATA); }

  bool empty() const { return IsData() && header_.payload_length == 0; }

  void set_payload_length(std::uint32_t length) {
    CHECK_LE(length, sizeof payload_data_);
    header_.message_type = Header::DATA;
    header_.payload_length = length;
  }

  std::uint32_t generation() const { return header_.generation; }

  void set_generation(std::uint32_t generation) {
    header_.generation = generation;
  }

  Payload& payload() { return payload_data_; }

  const Payload& payload() const { return payload_data_; }

  std::uint32_t payload_length() const { return header_.payload_length; }

  bool IsBegin() const { return header_.message_type == Header::BEGIN; }

  bool IsEnd() const { return header_.message_type == Header::END; }

  bool IsData() const { return header_.message_type == Header::DATA; }

  char* raw_data() { return reinterpret_cast<char*>(this); }

  const char* raw_data() const { return reinterpret_cast<const char*>(this); }

  size_t raw_data_length() const { return payload_length() + sizeof header_; }
};

static_assert(sizeof(Packet) == layout::socket_forward::kMaxPacketSize, "");
static_assert(std::is_pod<Packet>{}, "");

// Data sent will start with a uint32_t indicating the number of bytes being
// sent, followed be the data itself
class SocketForwardRegionView
    : public TypedRegionView<SocketForwardRegionView,
                             layout::socket_forward::SocketForwardLayout> {
 private:
#ifdef CUTTLEFISH_HOST
  int AcquireConnectionID(int port);
#else
  int GetWaitingConnectionID();
#endif

  // Returns an empty data packet if the other side is closed.
  void Recv(int connection_id, Packet* packet);
  // Returns true on success
  bool Send(int connection_id, const Packet& packet);

  // skip everything in the connection queue until seeing a BEGIN for the
  // current generation
  void IgnoreUntilBegin(int connection_id, std::uint32_t generation);

  bool IsOtherSideRecvClosed(int connection_id);

  void ResetQueueStates(layout::socket_forward::QueuePair* queue_pair);

  void MarkQueueDisconnected(int connection_id,
                             layout::socket_forward::Queue
                                 layout::socket_forward::QueuePair::*direction);

 public:
  // Helper class that will send a ConnectionBegin marker when constructed and a
  // ConnectionEnd marker when destroyed.
  class Sender {
   public:
    explicit Sender(SocketForwardRegionView* view, int connection_id,
                    std::uint32_t generation)
        : view_{view, {connection_id, generation}},
          connection_id_{connection_id} {
      auto packet = Packet::MakeBegin();
      packet.set_generation(generation);
      view_->Send(connection_id, packet);
    }

    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;

    Sender(Sender&&) = default;
    Sender& operator=(Sender&&) = default;
    ~Sender() = default;

    // Returns true on success
    bool Send(const Packet& packet);
    int port() const { return view_->port(connection_id_); }

   private:
    bool closed() const;

    struct EndSender {
      int connection_id = -1;
      std::uint32_t generation{};
      void operator()(SocketForwardRegionView* view) const {
        if (view) {
          CHECK(connection_id >= 0);
          auto packet = Packet::MakeEnd();
          packet.set_generation(generation);
          view->Send(connection_id, packet);
          view->MarkSendQueueDisconnected(connection_id);
        }
      }
    };
    // Doesn't actually own the View, responsible for sending the End
    // indicator and marking the sending side as disconnected.
    std::unique_ptr<SocketForwardRegionView, EndSender> view_;
    int connection_id_{};
  };

  class Receiver {
   public:
    explicit Receiver(SocketForwardRegionView* view, int connection_id,
                      std::uint32_t generation)
        : view_{view, {connection_id}},
          connection_id_{connection_id},
          generation_{generation} {}
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    Receiver(Receiver&&) = default;
    Receiver& operator=(Receiver&&) = default;
    ~Receiver() = default;

    void Recv(Packet* packet);
    int port() const { return view_->port(connection_id_); }

   private:
    struct QueueCloser {
      int connection_id = -1;
      void operator()(SocketForwardRegionView* view) const {
        if (view) {
          CHECK(connection_id >= 0);
          view->MarkRecvQueueDisconnected(connection_id);
        }
      }
    };

    // Doesn't actually own the View, responsible for marking the receiving
    // side as disconnected
    std::unique_ptr<SocketForwardRegionView, QueueCloser> view_;
    int connection_id_{};
    std::uint32_t generation_{};
    bool got_begin_ = false;
  };

  SocketForwardRegionView() = default;
  ~SocketForwardRegionView() = default;
  SocketForwardRegionView(const SocketForwardRegionView&) = delete;
  SocketForwardRegionView& operator=(const SocketForwardRegionView&) = delete;

#ifdef CUTTLEFISH_HOST
  std::pair<Sender, Receiver> OpenConnection(int port);
#else
  std::pair<Sender, Receiver> AcceptConnection();
#endif

  int port(int connection_id);
  std::uint32_t generation();
  void CleanUpPreviousConnections();
  void MarkSendQueueDisconnected(int connection_id);
  void MarkRecvQueueDisconnected(int connection_id);

 private:
#ifndef CUTTLEFISH_HOST
  std::uint32_t last_seq_number_{};
#endif
};

}  // namespace socket_forward
}  // namespace vsoc
