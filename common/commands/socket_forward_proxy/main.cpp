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

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include <unistd.h>

#include "common/vsoc/lib/socket_forward_region_view.h"
#include "common/libs/tcp_socket/tcp_socket.h"

#ifdef CUTTLEFISH_HOST
#include "host/libs/config/host_config.h"
#endif

using vsoc::socket_forward::SocketForwardRegionView;

#ifdef CUTTLEFISH_HOST
DEFINE_uint32(port, 0, "Port from which to forward TCP connections.");
#endif

namespace {
class Worker {
 public:
  Worker(SocketForwardRegionView::Connection shm_connection,
         cvd::ClientSocket socket)
      : shm_connection_(std::move(shm_connection)),
        socket_(std::move(socket)){}

  [[nodiscard]] bool closed() {
    {
      std::lock_guard<std::mutex> guard(closed_lock_);
      if (closed_) {
        return true;
      }
    }
    if (shm_connection_.closed() || socket_.closed()) {
      std::lock_guard<std::mutex> guard(closed_lock_);
      closed_ = true;
    }
    return closed_;
  }

  void close() {
    std::lock_guard<std::mutex> guard(closed_lock_);
    closed_ = true;
  }

  static void SocketToShm(std::shared_ptr<Worker> worker) {
    worker->SocketToShmImpl();
  }

  static void ShmToSocket(std::shared_ptr<Worker> worker) {
    worker->ShmToSocketImpl();
  }

 private:
  void SocketToShmImpl() {
    constexpr int kRecvSize = 8192;

    auto sender = shm_connection_.MakeSender();

    while (true) {
      if (closed()) {
        break;
      }
      auto msg = socket_.RecvAny(kRecvSize);
      if (msg.empty()) {
        break;
      }
      sender.Send(std::move(msg));
    }
    LOG(INFO) << "Socket to shm exiting";
    close();
  }

  void ShmToSocketImpl() {
    auto receiver = shm_connection_.MakeReceiver();
    while (true) {
      if (closed()) {
        break;
      }
      auto msg = receiver.Recv();
      if (msg.empty() || socket_.closed()) {
        break;
      }
      if (socket_.Send(msg) < 0) {
        break;
      }
    }
    LOG(INFO) << "Shm to socket exiting";
    close();
  }

  SocketForwardRegionView::Connection shm_connection_;
  cvd::ClientSocket socket_;
  bool closed_{};
  std::mutex closed_lock_;
};

// One thread for reading from shm and writing into a socket.
// One thread for reading from a socket and writing into shm.
void LaunchWorkers(SocketForwardRegionView::Connection conn,
                   cvd::ClientSocket socket) {
  auto worker = std::make_shared<Worker>(std::move(conn), std::move(socket));
  std::thread threads[] = {std::thread(Worker::SocketToShm, worker),
                           std::thread(Worker::ShmToSocket, worker)};
  for (auto&& t : threads) {
    t.detach();
  }
}

#ifdef CUTTLEFISH_HOST
[[noreturn]] void host(SocketForwardRegionView* shm, int port) {
  LOG(INFO) << "starting server on " << port;
  cvd::ServerSocket server(port);
  while (true) {
    auto client_socket = server.Accept();
    LOG(INFO) << "client socket accepted";
    auto conn = shm->OpenConnection(port);
    LOG(INFO) << "shm connection opened";
    LaunchWorkers(std::move(conn), std::move(client_socket));
  }
}
#else
[[noreturn]] void guest(SocketForwardRegionView* shm) {
  LOG(INFO) << "Starting guest mainloop";
  while (true) {
    auto conn = shm->AcceptConnection();
    LOG(INFO) << "shm connection accepted";
    auto sock = cvd::ClientSocket(conn.port());
    LOG(INFO) << "socket opened to " << conn.port();
    LaunchWorkers(std::move(conn), std::move(sock));
  }
}
#endif

std::shared_ptr<SocketForwardRegionView> GetShm() {
  auto shm = SocketForwardRegionView::GetInstance(
#ifdef CUTTLEFISH_HOST
      vsoc::GetDomain().c_str()
#endif
  );
  if (!shm) {
    LOG(FATAL) << "Could not open SHM. Aborting.";
  }
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
  CHECK_NE(FLAGS_port, 0u) << "Must specify --port flag";
  host(shm.get(), FLAGS_port);
#else
  guest(shm.get());
#endif
}
