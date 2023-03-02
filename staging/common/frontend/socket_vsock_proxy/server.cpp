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

#include <set>

#include "common/frontend/socket_vsock_proxy/server.h"
#include "common/libs/utils/contains.h"

namespace cuttlefish {
namespace socket_proxy {
namespace {

bool socketErrorIsRecoverable(int error) {
  std::set<int> unrecoverable{EACCES, EAFNOSUPPORT, EINVAL, EPROTONOSUPPORT};
  return !Contains(unrecoverable, error);
}

[[noreturn]] static void SleepForever() {
  while (true) {
    sleep(std::numeric_limits<unsigned int>::max());
  }
}

}

TcpServer::TcpServer(int port) : port_(port) {}

SharedFD TcpServer::Start() {
  SharedFD server;

  server = SharedFD::SocketLocalServer(port_, SOCK_STREAM);
  CHECK(server->IsOpen()) << "Could not start server on " << port_;

  return server;
}

std::string TcpServer::Describe() const {
  return fmt::format("tcp: {}", port_);
}

VsockServer::VsockServer(int port) : port_(port) {}

// Intended to run in the guest
SharedFD VsockServer::Start() {
  SharedFD server;

  do {
    server = SharedFD::VsockServer(port_, SOCK_STREAM);
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

SharedFD DupServer::Start() {
  CHECK(sfd_->IsOpen()) << "Could not start duplicate server for passed fd";
  return sfd_;
}

std::string DupServer::Describe() const {
  return fmt::format("fd: {}", fd_);
}

}
}