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
#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"

constexpr std::size_t kMaxPacketSize = 8192;

DEFINE_string(server, "",
              "The type of server to host, `vsock` or `tcp`. When hosting a server "
              "of one type, the proxy will take inbound connections of this type and "
              "make outbound connections of the other type.");
DEFINE_uint32(tcp_port, 0, "TCP port");
DEFINE_uint32(vsock_port, 0, "vsock port");
DEFINE_uint32(vsock_cid, 0, "Vsock cid to initiate connections to");

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

  ssize_t SendAll(const std::vector<char>& packet) {
    ssize_t written{};
    while (written < static_cast<ssize_t>(packet.size())) {
      if (!socket_->IsOpen()) {
        return -1;
      }
      auto just_written =
          socket_->Send(packet.data() + written,
                        packet.size() - written, MSG_NOSIGNAL);
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
  void Recv(std::vector<char>* packet) {
    auto size = socket_->Read(packet->data(), packet->size());
    if (size < 0) {
      size = 0;
    }
    packet->resize(size);
  }

 private:
  cvd::SharedFD socket_;
};

void SocketToVsock(SocketReceiver socket_receiver,
                   SocketSender vsock_sender) {
  while (true) {
    std::vector<char> packet(kMaxPacketSize, '\0');
    socket_receiver.Recv(&packet);
    if (packet.empty() || vsock_sender.SendAll(packet) < 0) {
      break;
    }
  }
  LOG(INFO) << "Socket to vsock exiting";
}

void VsockToSocket(SocketSender socket_sender,
                   SocketReceiver vsock_receiver) {
  std::vector<char> packet(kMaxPacketSize, '\0');
  while (true) {
    vsock_receiver.Recv(&packet);
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

[[noreturn]] void TcpServer() {
  LOG(INFO) << "starting TCP server on " << FLAGS_tcp_port << " for vsock port "
            << FLAGS_vsock_port;
  auto server = cvd::SharedFD::SocketLocalServer(FLAGS_tcp_port, SOCK_STREAM);
  CHECK(server->IsOpen()) << "Could not start server on " << FLAGS_tcp_port;
  LOG(INFO) << "Accepting client connections";
  int last_failure_reason = 0;
  while (true) {
    auto client_socket = cvd::SharedFD::Accept(*server);
    CHECK(client_socket->IsOpen()) << "error creating client socket";
    cvd::SharedFD vsock_socket = cvd::SharedFD::VsockClient(
        FLAGS_vsock_cid, FLAGS_vsock_port, SOCK_STREAM);
    if (vsock_socket->IsOpen()) {
      last_failure_reason = 0;
      LOG(INFO) << "Connected to vsock:" << FLAGS_vsock_cid << ":"
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

[[noreturn]] void VsockServer() {
  LOG(INFO) << "Starting vsock server on " << FLAGS_vsock_port;
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

}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  CHECK(FLAGS_tcp_port != 0) << "Must specify -tcp_port flag";
  CHECK(FLAGS_vsock_port != 0) << "Must specify -vsock_port flag";
  if (FLAGS_server == "tcp") {
    CHECK(FLAGS_vsock_cid != 0) << "Must specify -vsock_cid flag";
    TcpServer();
  } else if (FLAGS_server == "vsock") {
    VsockServer();
  } else {
    LOG(FATAL) << "Unknown server type: " << FLAGS_server;
  }
}
