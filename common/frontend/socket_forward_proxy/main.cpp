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

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include <unistd.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/strings/str_split.h"
#include "common/vsoc/lib/socket_forward_region_view.h"

#ifdef CUTTLEFISH_HOST
#include "host/libs/config/cuttlefish_config.h"
#endif

using vsoc::socket_forward::Packet;
using vsoc::socket_forward::SocketForwardRegionView;

#ifdef CUTTLEFISH_HOST
DEFINE_string(guest_ports, "",
              "Comma-separated list of ports on which to forward TCP "
              "connections to the guest.");
DEFINE_string(host_ports, "",
              "Comma-separated list of ports on which to run TCP servers on "
              "the host.");
#endif

namespace {
// Sends packets, Shutdown(SHUT_WR) on destruction
class SocketSender {
 public:
  explicit SocketSender(cvd::SharedFD socket) : socket_{std::move(socket)} {}

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
  explicit SocketReceiver(cvd::SharedFD socket) : socket_{std::move(socket)} {}

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

void SocketToShm(SocketReceiver socket_receiver,
                 SocketForwardRegionView::ShmSender shm_sender) {
  while (true) {
    auto packet = Packet::MakeData();
    socket_receiver.Recv(&packet);
    if (packet.empty() || !shm_sender.Send(packet)) {
      break;
    }
  }
  LOG(INFO) << "Socket to shm exiting";
}

void ShmToSocket(SocketSender socket_sender,
                 SocketForwardRegionView::ShmReceiver shm_receiver) {
  auto packet = Packet{};
  while (true) {
    shm_receiver.Recv(&packet);
    CHECK(packet.IsData());
    if (packet.empty()) {
      break;
    }
    if (socket_sender.SendAll(packet) < 0) {
      break;
    }
  }
  LOG(INFO) << "Shm to socket exiting";
}

// One thread for reading from shm and writing into a socket.
// One thread for reading from a socket and writing into shm.
void HandleConnection(SocketForwardRegionView::ShmSenderReceiverPair shm_sender_and_receiver,
                      cvd::SharedFD socket) {
  auto socket_to_shm =
      std::thread(SocketToShm, SocketReceiver{socket}, std::move(shm_sender_and_receiver.first));
  ShmToSocket(SocketSender{socket}, std::move(shm_sender_and_receiver.second));
  socket_to_shm.join();
}

#ifdef CUTTLEFISH_HOST
struct PortPair {
  int guest_port;
  int host_port;
};

enum class QueueState {
  kFree,
  kUsed,
};

struct SocketConnectionInfo {
  std::mutex lock{};
  std::condition_variable cv{};
  cvd::SharedFD socket{};
  int guest_port{};
  QueueState state = QueueState::kFree;
};

static constexpr auto kNumHostThreads =
    vsoc::layout::socket_forward::kNumQueues;

using SocketConnectionInfoCollection =
    std::array<SocketConnectionInfo, kNumHostThreads>;

void MarkAsFree(SocketConnectionInfo* conn) {
  std::lock_guard<std::mutex> guard{conn->lock};
  conn->socket = cvd::SharedFD{};
  conn->guest_port = 0;
  conn->state = QueueState::kFree;
}

std::pair<int, cvd::SharedFD> WaitForConnection(SocketConnectionInfo* conn) {
  std::unique_lock<std::mutex> guard{conn->lock};
  while (conn->state != QueueState::kUsed) {
    conn->cv.wait(guard);
  }
  return {conn->guest_port, conn->socket};
}

[[noreturn]] void host_thread(SocketForwardRegionView::ShmConnectionView view,
                              SocketConnectionInfo* conn) {
  while (true) {
    int guest_port{};
    cvd::SharedFD socket{};
    // TODO structured binding in C++17
    std::tie(guest_port, socket) = WaitForConnection(conn);

    LOG(INFO) << "Establishing connection to guest port " << guest_port
              << " with connection_id: " << view.connection_id();
    HandleConnection(view.EstablishConnection(guest_port), std::move(socket));
    LOG(INFO) << "Connection to guest port " << guest_port
              << " closed. Marking queue " << view.connection_id()
              << " as free.";
    MarkAsFree(conn);
  }
}

bool TryAllocateConnection(SocketConnectionInfo* conn, int guest_port,
                           cvd::SharedFD socket) {
  bool success = false;
  {
    std::lock_guard<std::mutex> guard{conn->lock};
    if (conn->state == QueueState::kFree) {
      conn->socket = std::move(socket);
      conn->guest_port = guest_port;
      conn->state = QueueState::kUsed;
      success = true;
    }
  }
  if (success) {
    conn->cv.notify_one();
  }
  return success;
}

void AllocateWorkers(cvd::SharedFD socket,
                     SocketConnectionInfoCollection* socket_connection_info,
                     int guest_port) {
  while (true) {
    for (auto& conn : *socket_connection_info) {
      if (TryAllocateConnection(&conn, guest_port, socket)) {
        return;
      }
    }
    LOG(INFO) << "no queues available. sleeping and retrying";
    sleep(5);
  }
}

[[noreturn]] void host_impl(
    SocketForwardRegionView* shm,
    SocketConnectionInfoCollection* socket_connection_info,
    std::vector<PortPair> ports, std::size_t index) {
  // launch a worker for the following port before handling the current port.
  // recursion (instead of a loop) removes the need fore any join() or having
  // the main thread do no work.
  if (index + 1 < ports.size()) {
    std::thread(host_impl, shm, socket_connection_info, ports, index + 1)
        .detach();
  }
  auto guest_port = ports[index].guest_port;
  auto host_port = ports[index].host_port;
  LOG(INFO) << "starting server on " << host_port << " for guest port "
            << guest_port;
  auto server = cvd::SharedFD::SocketLocalServer(host_port, SOCK_STREAM);
  CHECK(server->IsOpen()) << "Could not start server on port " << host_port;
  while (true) {
    auto client_socket = cvd::SharedFD::Accept(*server);
    CHECK(client_socket->IsOpen()) << "error creating client socket";
    LOG(INFO) << "client socket accepted";
    AllocateWorkers(std::move(client_socket), socket_connection_info,
                    guest_port);
  }
}

[[noreturn]] void host(SocketForwardRegionView* shm,
                       std::vector<PortPair> ports) {
  CHECK(!ports.empty());

  SocketConnectionInfoCollection socket_connection_info{};

  auto conn_info_iter = std::begin(socket_connection_info);
  for (auto& shm_connection_view : shm->AllConnections()) {
    CHECK_NE(conn_info_iter, std::end(socket_connection_info));
    std::thread(host_thread, std::move(shm_connection_view), &*conn_info_iter)
        .detach();
    ++conn_info_iter;
  }
  CHECK_EQ(conn_info_iter, std::end(socket_connection_info));
  host_impl(shm, &socket_connection_info, ports, 0);
}

std::vector<PortPair> ParsePortsList(const std::string& guest_ports_str,
                                     const std::string& host_ports_str) {
  std::vector<PortPair> ports{};
  auto guest_ports = cvd::StrSplit(guest_ports_str, ',');
  auto host_ports = cvd::StrSplit(host_ports_str, ',');
  CHECK(guest_ports.size() == host_ports.size());
  for (std::size_t i = 0; i < guest_ports.size(); ++i) {
    ports.push_back({std::stoi(guest_ports[i]), std::stoi(host_ports[i])});
  }
  return ports;
}

#else
cvd::SharedFD OpenSocketConnection(int port) {
  while (true) {
    auto sock = cvd::SharedFD::SocketLocalClient(port, SOCK_STREAM);
    if (sock->IsOpen()) {
      return sock;
    }
    LOG(WARNING) << "could not connect on port " << port
                 << ". sleeping for 1 second";
    sleep(1);
  }
}

[[noreturn]] void guest_thread(
    SocketForwardRegionView::ShmConnectionView view) {
  while (true) {
    LOG(INFO) << "waiting for new connection";
    auto shm_sender_and_receiver = view.WaitForNewConnection();
    LOG(INFO) << "new connection for port " << view.port();
    HandleConnection(std::move(shm_sender_and_receiver), OpenSocketConnection(view.port()));
    LOG(INFO) << "connection closed on port " << view.port();
  }
}

[[noreturn]] void guest(SocketForwardRegionView* shm) {
  LOG(INFO) << "Starting guest mainloop";
  auto connection_views = shm->AllConnections();
  for (auto&& shm_connection_view : connection_views) {
    std::thread(guest_thread, std::move(shm_connection_view)).detach();
  }
  while (true) {
    sleep(std::numeric_limits<unsigned int>::max());
  }
}

#endif

SocketForwardRegionView* GetShm() {
  auto shm = SocketForwardRegionView::GetInstance(
#ifdef CUTTLEFISH_HOST
      vsoc::GetDomain().c_str()
#endif
  );
  if (!shm) {
    LOG(FATAL) << "Could not open SHM. Aborting.";
  }
  shm->CleanUpPreviousConnections();
  return shm;
}

// makes sure we're running as root on the guest, no-op on the host
void assert_correct_user() {
#ifndef CUTTLEFISH_HOST
  CHECK_EQ(getuid(), 0u) << "must run as root!";
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  assert_correct_user();

  auto shm = GetShm();
  auto worker = shm->StartWorker();

#ifdef CUTTLEFISH_HOST
  CHECK(!FLAGS_guest_ports.empty()) << "Must specify --guest_ports flag";
  CHECK(!FLAGS_host_ports.empty()) << "Must specify --host_ports flag";
  host(shm, ParsePortsList(FLAGS_guest_ports, FLAGS_host_ports));
#else
  guest(shm);
#endif
}
