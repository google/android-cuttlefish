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
  static Packet MakeBegin() {
    return MakePacket(Header::BEGIN);
  }

  static Packet MakeEnd() {
    return MakePacket(Header::END);
  }

  static Packet MakeData() {
    return MakePacket(Header::DATA);
  }

  bool empty() const {
    return IsData() && header_.payload_length == 0;
  }

  void set_payload_length(std::uint32_t length) {
    CHECK_LE(length, sizeof payload_data_);
    header_.message_type = Header::DATA;
    header_.payload_length = length;
  }

  std::uint32_t generation() const {
    return header_.generation;
  }

  void set_generation(std::uint32_t generation) {
    header_.generation = generation;
  }

  Payload& payload() {
    return payload_data_;
  }

  const Payload& payload() const {
    return payload_data_;
  }

  std::uint32_t payload_length() const {
    return header_.payload_length;
  }

  bool IsBegin() const {
    return header_.message_type == Header::BEGIN;
  }

  bool IsEnd() const {
    return header_.message_type == Header::END;
  }

  bool IsData() const {
    return header_.message_type == Header::DATA;
  }

  char* raw_data() {
    return reinterpret_cast<char*>(this);
  }

  const char* raw_data() const {
    return reinterpret_cast<const char*>(this);
  }

  size_t raw_data_length() const {
    return payload_length() + sizeof header_;
  }
};

static_assert(sizeof(Packet) == layout::socket_forward::kMaxPacketSize, "");
static_assert(std::is_pod<Packet>{}, "");

// Data sent will start with a uint32_t indicating the number of bytes being
// sent, followed be the data itself
class SocketForwardRegionView
    : public TypedRegionView<
        SocketForwardRegionView,
        layout::socket_forward::SocketForwardLayout> {
 private:
#ifdef CUTTLEFISH_HOST
  int AcquireConnectionID(int port);
#endif
  void ReleaseConnectionID(int connection_id);
  std::pair<int, int> GetWaitingConnectionIDAndPort();

  // Returns an empty data packet if the other side is closed.
  void Recv(int connection_id, Packet* packet);
  // Does nothing if packet is empty
  void Send(int connection_id, const Packet& packet);

  void SendBegin(int connection_id);
  void SendEnd(int connection_id);

  // skip everything in the connection queue until seeing a BEGIN
  void IgnoreUntilBegin(int connection_id);

  bool IsOtherSideClosed(int connection_id);

 public:
  class Sender;
  class Receiver;

  // MakeSender and MakeReceiver may only be called once per connection.
  // Moving a Connection object invalidates any existing Sender or Receiver.
  class Connection {
    friend Receiver;
    friend Sender;
    friend SocketForwardRegionView;

   public:
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&&) = default;
    Connection& operator=(Connection&&) = default;
    ~Connection() = default;

    Sender MakeSender();
    Receiver MakeReceiver();

    int port() const {
      return port_;
    }
    bool closed() const;

   private:
    // Sends should be done using a Sender.
    void Send(const Packet& packet);
    void SendBegin();
    void SendEnd();

    // Receives should be done using a Receiver.
    void Recv(Packet* packet);
    void IgnoreUntilBegin();

    struct Releaser {
      int connection_id_;
      void operator()(SocketForwardRegionView* view) const {
        if (view) {
          view->ReleaseConnectionID(connection_id_);
        }
      }
    };

    Connection(SocketForwardRegionView* view, int connection_id, int port);

    // this is a little weird, but I'm using the unique_ptr to release the id
    // to the view, it's really representing ownership of the connection_id_
    std::unique_ptr<SocketForwardRegionView, Releaser> view_{};
    int connection_id_ = -1;
    int port_ = -1;

    bool receiver_created_ = false;
    bool sender_created_ = false;
  };

  // Helper class that will send a ConnectionBegin marker when constructed and a
  // ConnectionEnd marker when destroyed.
  class Sender {
   public:
    explicit Sender(Connection* connection) : connection_{connection} {
      connection_->SendBegin();
    }

    void Send(const Packet& packet) {
      connection_->Send(packet);
    }

   private:
    struct EndSender {
      void operator()(Connection* connection) const {
        if (connection) {
          connection->SendEnd();
        }
      }
    };
    // Doesn't actually own the Connection, responsible for sending the End
    // indicator
    std::unique_ptr<Connection, EndSender> connection_;
  };

  // Helper class that will wait for a ConnectionBegin marker when constructed
  class Receiver {
   public:
    explicit Receiver(Connection* connection) : connection_{connection} {
      connection_->IgnoreUntilBegin();
    }

    void Recv(Packet* packet) {
      return connection_->Recv(packet);
    }

   private:
    Connection* connection_;
  };

  SocketForwardRegionView() = default;
  ~SocketForwardRegionView() = default;
  SocketForwardRegionView(const SocketForwardRegionView&) = delete;
  SocketForwardRegionView& operator=(const SocketForwardRegionView&) = delete;

#ifdef CUTTLEFISH_HOST
  Connection OpenConnection(int port);
#else
  Connection AcceptConnection();
#endif

 private:
  std::uint32_t last_seq_number_{};
};

}  // namespace socket_forward
}  // namespace vsoc
