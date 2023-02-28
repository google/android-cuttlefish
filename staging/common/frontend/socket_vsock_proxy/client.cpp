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

#include <android-base/strings.h>
#include <string>

#include "common/frontend/socket_vsock_proxy/client.h"

namespace cuttlefish {
namespace socket_proxy {
namespace {

bool IsIpv6(const std::string& address) {
  return address.find(':') != std::string::npos;
}

SharedFD StartIpv4(const std::string& host, int port) {
  return SharedFD::SocketClient(host, port, SOCK_STREAM);
}

SharedFD StartIpv6(const std::string& host, int port) {
  const auto host_parsed = android::base::Tokenize(host, "%");
  const auto host_interface_tokens_count = host_parsed.size();

  CHECK(host_interface_tokens_count == 1 || host_interface_tokens_count == 2)
      << "Cannot parse passed host " << host << " to extract the network interface separated by %";

  std::string host_name;
  std::string interface_name;
  if (host_parsed.size() == 2) {
    host_name = host_parsed[0];
    interface_name = host_parsed[1];
  } else {
    host_name = host;
  }

  return SharedFD::Socket6Client(host_name, interface_name, port, SOCK_STREAM);
}

}

TcpClient::TcpClient(std::string host, int port) : host_(std::move(host)), port_(port) {}

SharedFD TcpClient::Start() {
  SharedFD client;

  if (IsIpv6(host_)) {
    client = StartIpv6(host_, port_);
  } else {
    client = StartIpv4(host_, port_);
  }

  if (client->IsOpen()) {
    last_failure_reason_ = 0;
    LOG(DEBUG) << "Connected to socket:" << host_ << ":" << port_;
    return client;
  } else {
    // Don't log if the previous connection failed with the same error
    if (last_failure_reason_ != client->GetErrno()) {
      last_failure_reason_ = client->GetErrno();
      LOG(ERROR) << "Unable to connect to tcp server: " << client->StrError();
    }
  }

  return client;
}

std::string TcpClient::Describe() const {
  return fmt::format("tcp: {}:{}", host_, port_);
}

VsockClient::VsockClient(int id, int port) : id_(id), port_(port) {}

SharedFD VsockClient::Start() {
  auto vsock_socket = SharedFD::VsockClient(id_, port_, SOCK_STREAM);

  if (vsock_socket->IsOpen()) {
    last_failure_reason_ = 0;
    LOG(DEBUG) << "Connected to vsock:" << id_ << ":" << port_;
  } else {
    // Don't log if the previous connection failed with the same error
    if (last_failure_reason_ != vsock_socket->GetErrno()) {
      last_failure_reason_ = vsock_socket->GetErrno();
      LOG(ERROR) << "Unable to connect to vsock server: "
                 << vsock_socket->StrError();
    }
  }
  return vsock_socket;
}

std::string VsockClient::Describe() const {
  return fmt::format("vsock: {}:{}", id_, port_);
}

}
}