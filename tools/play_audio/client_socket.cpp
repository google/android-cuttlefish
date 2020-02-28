/*
 *
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
#include "client_socket.h"

#include "android-base/logging.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
std::uint16_t HostOrderUInt16(const void* src) {
  std::uint16_t result{};
  std::memcpy(&result, src, sizeof result);
  return htons(result);
}

std::uint32_t HostOrderUInt32(const void* src) {
  std::uint32_t result{};
  std::memcpy(&result, src, sizeof result);
  return htonl(result);
}
}  // namespace

using cfp::ClientSocket;

ClientSocket::ClientSocket(std::uint16_t port)
    : socket_fd_{socket(AF_INET, SOCK_STREAM, 0)} {
  sockaddr_in server_addr{};

  if (socket_fd_ < 0) {
    LOG(ERROR) << "couldn't create socket\n";
    return;
  }
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
    LOG(ERROR) << "couldn't convert localhost address\n";
    close();
    return;
  }

  if (connect(socket_fd_, reinterpret_cast<sockaddr*>(&server_addr),
              sizeof server_addr) < 0) {
    LOG(ERROR) << "connection failed\n";
    close();
    return;
  }
}

ClientSocket::~ClientSocket() { close(); }

ClientSocket::ClientSocket(ClientSocket&& other)
    : socket_fd_{other.socket_fd_} {
  other.socket_fd_ = -1;
}

ClientSocket& ClientSocket::operator=(ClientSocket&& other) {
  close();
  socket_fd_ = other.socket_fd_;
  other.socket_fd_ = -1;
  return *this;
}

std::vector<unsigned char> ClientSocket::RecvAll(ssize_t count) {
  std::vector<unsigned char> buf(count);
  size_t total_read = 0;
  while (total_read < buf.size()) {
    auto just_read =
        read(socket_fd_, buf.data() + total_read, buf.size() - total_read);
    if (just_read <= 0) {
      LOG(ERROR) << "read failed";
      return {};
    }
    total_read += static_cast<size_t>(just_read);
  }
  return buf;
}

std::uint16_t ClientSocket::RecvUInt16() {
  return HostOrderUInt16(RecvAll(sizeof(std::uint16_t)).data());
}

std::uint32_t ClientSocket::RecvUInt32() {
  return HostOrderUInt32(RecvAll(sizeof(std::uint32_t)).data());
}

void ClientSocket::close() {
  if (socket_fd_ >= 0) {
    ::close(socket_fd_);
    socket_fd_ = -1;
  }
}
