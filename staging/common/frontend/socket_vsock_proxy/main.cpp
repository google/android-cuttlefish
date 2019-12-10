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

#include <set>
#include <thread>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"

#ifdef CUTTLEFISH_HOST
#include "host/libs/config/cuttlefish_config.h"
#endif

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

constexpr std::size_t kMaxPacketSize = 8192;
constexpr std::size_t kMaxPayloadSize = kMaxPacketSize - sizeof(Header);

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

static_assert(sizeof(Packet) == kMaxPacketSize, "");
static_assert(std::is_pod<Packet>{}, "");

DEFINE_uint32(tcp_port, 0, "TCP port (server on host, client on guest)");
DEFINE_uint32(vsock_port, 0, "vsock port (client on host, server on guest");
DEFINE_uint32(vsock_guest_cid, 0, "Guest identifier");

namespace {
// Sends packets, Shutdown(SHUT_WR) on destruction
class SocketSender {
 public:
  explicit SocketSender(cvd::SharedFD socket) : socket_{socket} {}

  SocketSender(SocketSender&&) = default;
  SocketSender& operator=(SocketSender&&) = default;

  SocketSender(const SocketSender&&) = delete;
  SocketSender& operator=(const SocketSender&) = delete;

  ~SocketSender() {
    if (socket_.operator->()) {  // check that socket_ was not moved-from
      socket_->Shutdown(SHUT_WR);
    }
  }

  ssize_t SendAll(const Packet& packet) {
    ssize_t written{};
    while (written < static_cast<ssize_t>(packet.payload_length())) {
      if (!socket_->IsOpen()) {
        return -1;
      }
      auto just_written =
          socket_->Send(packet.payload() + written,
                        packet.payload_length() - written, MSG_NOSIGNAL);
      if (just_written <= 0) {
        LOG(INFO) << "Couldn't write to client: "
                  << strerror(socket_->GetErrno());
        return just_written;
      }
      written += just_written;
    }
    return written;
  }

 private:
  cvd::SharedFD socket_;
};

class SocketReceiver {
 public:
  explicit SocketReceiver(cvd::SharedFD socket) : socket_{socket} {}

  SocketReceiver(SocketReceiver&&) = default;
  SocketReceiver& operator=(SocketReceiver&&) = default;

  SocketReceiver(const SocketReceiver&&) = delete;
  SocketReceiver& operator=(const SocketReceiver&) = delete;

  // *packet will be empty if Read returns 0 or error
  void Recv(Packet* packet) {
    auto size = socket_->Read(packet->payload(), sizeof packet->payload());
    if (size < 0) {
      size = 0;
    }
    packet->set_payload_length(size);
  }

 private:
  cvd::SharedFD socket_;
};

void SocketToVsock(SocketReceiver socket_receiver,
                   SocketSender vsock_sender) {
  while (true) {
    auto packet = Packet::MakeData();
    socket_receiver.Recv(&packet);
    if (packet.empty() || vsock_sender.SendAll(packet) < 0) {
      break;
    }
  }
  LOG(INFO) << "Socket to vsock exiting";
}

void VsockToSocket(SocketSender socket_sender,
                   SocketReceiver vsock_receiver) {
  auto packet = Packet::MakeData();
  while (true) {
    vsock_receiver.Recv(&packet);
    CHECK(packet.IsData());
    if (packet.empty()) {
      break;
    }
    if (socket_sender.SendAll(packet) < 0) {
      break;
    }
  }
  LOG(INFO) << "Vsock to socket exiting";
}

// One thread for reading from shm and writing into a socket.
// One thread for reading from a socket and writing into shm.
void HandleConnection(cvd::SharedFD vsock,
                      cvd::SharedFD socket) {
  auto socket_to_vsock =
      std::thread(SocketToVsock, SocketReceiver{socket}, SocketSender{vsock});
  VsockToSocket(SocketSender{socket}, SocketReceiver{vsock});
  socket_to_vsock.join();
}

#ifdef CUTTLEFISH_HOST
[[noreturn]] void host() {
  LOG(INFO) << "starting server on " << FLAGS_tcp_port << " for vsock port "
            << FLAGS_vsock_port;
  auto server = cvd::SharedFD::SocketLocalServer(FLAGS_tcp_port, SOCK_STREAM);
  CHECK(server->IsOpen()) << "Could not start server on " << FLAGS_tcp_port;
  LOG(INFO) << "Accepting client connections";
  int last_failure_reason = 0;
  while (true) {
    auto client_socket = cvd::SharedFD::Accept(*server);
    CHECK(client_socket->IsOpen()) << "error creating client socket";
    cvd::SharedFD vsock_socket = cvd::SharedFD::VsockClient(
        FLAGS_vsock_guest_cid, FLAGS_vsock_port, SOCK_STREAM);
    if (vsock_socket->IsOpen()) {
      last_failure_reason = 0;
      LOG(INFO) << "Connected to vsock:" << FLAGS_vsock_guest_cid << ":"
                << FLAGS_vsock_port;
    } else {
      // Don't log if the previous connection failed with the same error
      if (last_failure_reason != vsock_socket->GetErrno()) {
        last_failure_reason = vsock_socket->GetErrno();
        LOG(ERROR) << "Unable to connect to vsock server: "
                   << vsock_socket->StrError();
      }
      continue;
    }
    auto thread = std::thread(HandleConnection, std::move(vsock_socket),
                              std::move(client_socket));
    thread.detach();
  }
}

#else
cvd::SharedFD OpenSocketConnection() {
  while (true) {
    auto sock = cvd::SharedFD::SocketLocalClient(FLAGS_tcp_port, SOCK_STREAM);
    if (sock->IsOpen()) {
      return sock;
    }
    LOG(WARNING) << "could not connect on port " << FLAGS_tcp_port
                 << ". sleeping for 1 second";
    sleep(1);
  }
}

bool socketErrorIsRecoverable(int error) {
  std::set<int> unrecoverable{EACCES, EAFNOSUPPORT, EINVAL, EPROTONOSUPPORT};
  return unrecoverable.find(error) == unrecoverable.end();
}

[[noreturn]] static void SleepForever() {
  while (true) {
    sleep(std::numeric_limits<unsigned int>::max());
  }
}

[[noreturn]] void guest() {
  LOG(INFO) << "Starting guest mainloop";
  LOG(INFO) << "starting server on " << FLAGS_vsock_port;
  cvd::SharedFD vsock;
  do {
    vsock = cvd::SharedFD::VsockServer(FLAGS_vsock_port, SOCK_STREAM);
    if (!vsock->IsOpen() && !socketErrorIsRecoverable(vsock->GetErrno())) {
      LOG(ERROR) << "Could not open vsock socket: " << vsock->StrError();
      SleepForever();
    }
  } while (!vsock->IsOpen());
  CHECK(vsock->IsOpen()) << "Could not start server on " << FLAGS_vsock_port;
  while (true) {
    LOG(INFO) << "waiting for vsock connection";
    auto vsock_client = cvd::SharedFD::Accept(*vsock);
    CHECK(vsock_client->IsOpen()) << "error creating vsock socket";
    LOG(INFO) << "vsock socket accepted";
    auto client = OpenSocketConnection();
    CHECK(client->IsOpen()) << "error connecting to guest client";
    auto thread = std::thread(HandleConnection, std::move(vsock_client),
                              std::move(client));
    thread.detach();
  }
}

#endif
}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  CHECK(FLAGS_tcp_port != 0) << "Must specify -tcp_port flag";
  CHECK(FLAGS_vsock_port != 0) << "Must specify -vsock_port flag";
#ifdef CUTTLEFISH_HOST
  CHECK(FLAGS_vsock_guest_cid != 0) << "Must specify -vsock_guest_cid flag";
  host();
#else
  guest();
#endif
}
