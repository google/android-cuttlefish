//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>
#include <set>
#include <thread>

#include "common/frontend/socket_vsock_proxy/server.h"
#include "common/libs/utils/contains.h"

namespace cuttlefish {
namespace socket_proxy {
namespace {

bool socketErrorIsRecoverable(int error) {
  std::set<int> unrecoverable{EACCES, EAFNOSUPPORT, EINVAL, EPROTONOSUPPORT, EADDRINUSE};
  return !Contains(unrecoverable, error);
}

[[noreturn]] static void SleepForever() {
  while (true) {
    sleep(std::numeric_limits<unsigned int>::max());
  }
}

}

TcpServer::TcpServer(int port, int retries_count, std::chrono::milliseconds retries_delay)
    : port_(port),
      retries_count_(retries_count),
      retries_delay_(retries_delay) {};

Result<SharedFD> TcpServer::Start() {
  SharedFD server;
  int last_error = 0;

  for (int i = 0; i < retries_count_; i++) {
    server = SharedFD::SocketLocalServer(port_, SOCK_STREAM);
    if (server->IsOpen()) {
      return server;
    }
    last_error = server->GetErrno();

    LOG(INFO) << "Failed to start TCP server on port: " << port_
              << " after attempt #" << i + 1 << " (going to have "
              << retries_count_ << " total attempts). Error: " << last_error;

    std::this_thread::sleep_for(retries_delay_);
  }

  return CF_ERR("Could not start TCP server on port: " << port_
                << "after " << retries_count_ << " attempts. Last error: " << last_error);
}

std::string TcpServer::Describe() const {
  return fmt::format("tcp: {}", port_);
}

VsockServer::VsockServer(int port, std::optional<int> vhost_user_vsock_cid)
    : port_(port), vhost_user_vsock_cid_(vhost_user_vsock_cid) {}

Result<SharedFD> VsockServer::Start() {
  SharedFD server;
  do {
    server = SharedFD::VsockServer(port_, SOCK_STREAM, vhost_user_vsock_cid_);
    if (!server->IsOpen() && !socketErrorIsRecoverable(server->GetErrno())) {
      LOG(ERROR) << "Could not open vsock socket: " << server->StrError();
      // socket_vsock_proxy will now wait forever in the guest on encountering an
      // "unrecoverable" errno. This is to prevent churn from being restarted by
      // init.vsoc.rc.
      SleepForever();
    }
  } while (!server->IsOpen());

  return server;
}

std::string VsockServer::Describe() const {
  return fmt::format("vsock: {}", port_);
}

DupServer::DupServer(int fd) : fd_(fd), sfd_(SharedFD::Dup(fd_)) {
  close(fd);
}

Result<SharedFD> DupServer::Start() {
  CF_EXPECT(sfd_->IsOpen(), "Could not start duplicate server for passed fd");
  return sfd_;
}

std::string DupServer::Describe() const {
  return fmt::format("fd: {}", fd_);
}

}
}  // namespace cuttlefish
