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

#include <cstdlib>
#include <utility>
#include <vector>
#include <memory>

#include "common/vsoc/lib/typed_region_view.h"
#include "common/vsoc/shm/socket_forward_layout.h"

namespace vsoc {
namespace socket_forward {

struct Header {
  std::uint32_t payload_length;
  enum MessageType : std::uint32_t {
    DATA = 0,
    BEGIN,
    END,
    RECV_CLOSED,  // indicate that this side's receive end is closed
    RESTART,
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

  static constexpr Packet MakePacket(Header::MessageType type) {
    Packet packet{};
    packet.header_.message_type = type;
    return packet;
  }

 public:
  // port is only revelant on the host-side.
  static Packet MakeBegin(std::uint16_t port);

  static constexpr Packet MakeEnd() { return MakePacket(Header::END); }

  static constexpr Packet MakeRecvClosed() {
    return MakePacket(Header::RECV_CLOSED);
  }

  static constexpr Packet MakeRestart() { return MakePacket(Header::RESTART); }

  // NOTE payload and payload_length must still be set.
  static constexpr Packet MakeData() { return MakePacket(Header::DATA); }

  bool empty() const { return IsData() && header_.payload_length == 0; }

  void set_payload_length(std::uint32_t length) {
    CHECK_LE(length, sizeof payload_data_);
    header_.payload_length = length;
  }

  Payload& payload() { return payload_data_; }

  const Payload& payload() const { return payload_data_; }

  constexpr std::uint32_t payload_length() const {
    return header_.payload_length;
  }

  constexpr bool IsBegin() const {
    return header_.message_type == Header::BEGIN;
  }

  constexpr bool IsEnd() const { return header_.message_type == Header::END; }

  constexpr bool IsData() const { return header_.message_type == Header::DATA; }

  constexpr bool IsRecvClosed() const {
    return header_.message_type == Header::RECV_CLOSED;
  }

  constexpr bool IsRestart() const {
    return header_.message_type == Header::RESTART;
  }

  constexpr std::uint16_t port() const {
    CHECK(IsBegin());
    std::uint16_t port_number{};
    CHECK_EQ(payload_length(), sizeof port_number);
    std::memcpy(&port_number, payload(), sizeof port_number);
    return port_number;
  }

  char* raw_data() { return reinterpret_cast<char*>(this); }

  const char* raw_data() const { return reinterpret_cast<const char*>(this); }

  constexpr size_t raw_data_length() const {
    return payload_length() + sizeof header_;
  }
};

static_assert(sizeof(Packet) == layout::socket_forward::kMaxPacketSize, "");
static_assert(std::is_pod<Packet>{}, "");

// Data sent will start with a uint32_t indicating the number of bytes being
// sent, followed be the data itself
class SocketForwardRegionView
    : public TypedRegionView<SocketForwardRegionView,
                             layout::socket_forward::SocketForwardLayout> {
 private:
  // Returns an empty data packet if the other side is closed.
  void Recv(int connection_id, Packet* packet);
  // Returns true on success
  bool Send(int connection_id, const Packet& packet);

  // skip everything in the connection queue until seeing a BEGIN packet.
  // returns port from begin packet.
  int IgnoreUntilBegin(int connection_id);

 public:
  class ShmSender;
  class ShmReceiver;

  using ShmSenderReceiverPair = std::pair<ShmSender, ShmReceiver>;

  class ShmConnectionView {
   public:
    ShmConnectionView(SocketForwardRegionView* region_view, int connection_id)
        : region_view_{region_view}, connection_id_{connection_id} {}

#ifdef CUTTLEFISH_HOST
    ShmSenderReceiverPair EstablishConnection(int port);
#else
    // Should not be called while there is an active ShmSender or ShmReceiver
    // for this connection.
    ShmSenderReceiverPair WaitForNewConnection();
#endif

    int port() const { return port_; }

    bool Send(const Packet& packet);
    void Recv(Packet* packet);

    ShmConnectionView(const ShmConnectionView&) = delete;
    ShmConnectionView& operator=(const ShmConnectionView&) = delete;

    // Moving invalidates all existing ShmSenders and ShmReceiver
    ShmConnectionView(ShmConnectionView&&) = default;
    ShmConnectionView& operator=(ShmConnectionView&&) = default;
    ~ShmConnectionView() = default;

    // NOTE should only be used for debugging/logging purposes.
    // connection_ids are an implementation detail that are currently useful for
    // debugging, but may go away in the future.
    int connection_id() const { return connection_id_; }

   private:
    SocketForwardRegionView* region_view() const { return region_view_; }

    bool IsOtherSideRecvClosed() {
      std::lock_guard<std::mutex> guard(*other_side_receive_closed_lock_);
      return other_side_receive_closed_;
    }

    void MarkOtherSideRecvClosed() {
      std::lock_guard<std::mutex> guard(*other_side_receive_closed_lock_);
      other_side_receive_closed_ = true;
    }

    void ReceiverThread();
    ShmSenderReceiverPair ResetAndConnect();

    class Receiver {
     public:
      Receiver(ShmConnectionView* view)
          : view_{view}
      {
        receiver_thread_ = std::thread([this] { Start(); });
      }

      void Recv(Packet* packet);

      void Join() { receiver_thread_.join(); }

      Receiver(const Receiver&) = delete;
      Receiver& operator=(const Receiver&) = delete;

      ~Receiver() = default;
     private:
      void Start();
      bool GotRecvClosed() const;
      void ReceivePacket();
      void CheckPacketForRecvClosed();
      void CheckPacketForEnd();
      void UpdatePacketAndSignalAvailable();
      bool ShouldReceiveAnotherPacket() const;
      bool ExpectMorePackets() const;

      std::mutex receive_thread_data_lock_;
      std::condition_variable receive_thread_data_cv_;
      bool received_packet_free_ = true;
      Packet received_packet_{};

      ShmConnectionView* view_{};
      bool saw_recv_closed_ = false;
      bool saw_end_ = false;
#ifdef CUTTLEFISH_HOST
      bool saw_data_ = false;
#endif

      std::thread receiver_thread_;
    };

    SocketForwardRegionView* region_view_{};
    int connection_id_ = -1;
    int port_ = -1;

    std::unique_ptr<std::mutex> other_side_receive_closed_lock_ =
        std::unique_ptr<std::mutex>{new std::mutex{}};
    bool other_side_receive_closed_ = false;

    std::unique_ptr<Receiver> receiver_;
  };

  class ShmSender {
   public:
    explicit ShmSender(ShmConnectionView* view) : view_{view} {}

    ShmSender(const ShmSender&) = delete;
    ShmSender& operator=(const ShmSender&) = delete;

    ShmSender(ShmSender&&) = default;
    ShmSender& operator=(ShmSender&&) = default;
    ~ShmSender() = default;

    // Returns true on success
    bool Send(const Packet& packet);

   private:
    struct EndSender {
      void operator()(ShmConnectionView* view) const {
        if (view) {
          view->Send(Packet::MakeEnd());
        }
      }
    };

    // Doesn't actually own the View, responsible for sending the End
    // indicator and marking the sending side as disconnected.
    std::unique_ptr<ShmConnectionView, EndSender> view_;
  };

  class ShmReceiver {
   public:
    explicit ShmReceiver(ShmConnectionView* view) : view_{view} {}
    ShmReceiver(const ShmReceiver&) = delete;
    ShmReceiver& operator=(const ShmReceiver&) = delete;

    ShmReceiver(ShmReceiver&&) = default;
    ShmReceiver& operator=(ShmReceiver&&) = default;
    ~ShmReceiver() = default;

    void Recv(Packet* packet);

   private:
    struct RecvClosedSender {
      void operator()(ShmConnectionView* view) const {
        if (view) {
          view->Send(Packet::MakeRecvClosed());
        }
      }
    };

    // Doesn't actually own the view, responsible for sending the RecvClosed
    // indicator
    std::unique_ptr<ShmConnectionView, RecvClosedSender> view_{};
  };

  friend ShmConnectionView;

  SocketForwardRegionView() = default;
  ~SocketForwardRegionView() = default;
  SocketForwardRegionView(const SocketForwardRegionView&) = delete;
  SocketForwardRegionView& operator=(const SocketForwardRegionView&) = delete;

  using ConnectionViewCollection = std::vector<ShmConnectionView>;
  ConnectionViewCollection AllConnections();

  int port(int connection_id);
  void CleanUpPreviousConnections();

 private:
#ifndef CUTTLEFISH_HOST
  std::uint32_t last_seq_number_{};
#endif
};

}  // namespace socket_forward
}  // namespace vsoc
