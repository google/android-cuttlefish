/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "common/libs/tcp_socket/tcp_socket.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cerrno>

#include <android-base/logging.h>

namespace cuttlefish {

ClientSocket::ClientSocket(int port)
    : fd_(SharedFD::SocketLocalClient(port, SOCK_STREAM)) {}

Message ClientSocket::RecvAny(std::size_t length) {
  Message buf(length);
  auto read_count = fd_->Read(buf.data(), buf.size());
  if (read_count < 0) {
    read_count = 0;
  }
  buf.resize(read_count);
  return buf;
}

bool ClientSocket::closed() const {
  std::lock_guard<std::mutex> guard(closed_lock_);
  return other_side_closed_;
}

Message ClientSocket::Recv(std::size_t length) {
  Message buf(length);
  ssize_t total_read = 0;
  while (total_read < static_cast<ssize_t>(length)) {
    auto just_read = fd_->Read(&buf[total_read], buf.size() - total_read);
    if (just_read <= 0) {
      if (just_read < 0) {
        LOG(ERROR) << "read() error: " << strerror(errno);
      }
      {
        std::lock_guard<std::mutex> guard(closed_lock_);
        other_side_closed_ = true;
      }
      return Message{};
    }
    total_read += just_read;
  }
  CHECK(total_read == static_cast<ssize_t>(length));
  return buf;
}

ssize_t ClientSocket::SendNoSignal(const uint8_t* data, std::size_t size) {
  std::lock_guard<std::mutex> lock(send_lock_);
  ssize_t written{};
  while (written < static_cast<ssize_t>(size)) {
    if (!fd_->IsOpen()) {
      LOG(ERROR) << "fd_ is closed";
    }
    auto just_written = fd_->Send(data + written, size - written, MSG_NOSIGNAL);
    if (just_written <= 0) {
      LOG(INFO) << "Couldn't write to client: " << strerror(errno);
      {
        std::lock_guard<std::mutex> guard(closed_lock_);
        other_side_closed_ = true;
      }
      return just_written;
    }
    written += just_written;
  }
  return written;
}

ssize_t ClientSocket::SendNoSignal(const Message& message) {
  return SendNoSignal(&message[0], message.size());
}

ServerSocket::ServerSocket(int port)
    : fd_{SharedFD::SocketLocalServer(port, SOCK_STREAM)} {
  if (!fd_->IsOpen()) {
    LOG(FATAL) << "Couldn't open streaming server on port " << port;
  }
}

ClientSocket ServerSocket::Accept() {
  SharedFD client = SharedFD::Accept(*fd_);
  if (!client->IsOpen()) {
    LOG(FATAL) << "Error attemping to accept: " << strerror(errno);
  }
  return ClientSocket{client};
}

void AppendInNetworkByteOrder(Message* msg, const std::uint8_t b) {
  msg->push_back(b);
}

void AppendInNetworkByteOrder(Message* msg, const std::uint16_t s) {
  const std::uint16_t n = htons(s);
  auto p = reinterpret_cast<const std::uint8_t*>(&n);
  msg->insert(msg->end(), p, p + sizeof n);
}

void AppendInNetworkByteOrder(Message* msg, const std::uint32_t w) {
  const std::uint32_t n = htonl(w);
  auto p = reinterpret_cast<const std::uint8_t*>(&n);
  msg->insert(msg->end(), p, p + sizeof n);
}

void AppendInNetworkByteOrder(Message* msg, const std::int32_t w) {
  std::uint32_t u{};
  std::memcpy(&u, &w, sizeof u);
  AppendInNetworkByteOrder(msg, u);
}

void AppendInNetworkByteOrder(Message* msg, const std::string& str) {
  msg->insert(msg->end(), str.begin(), str.end());
}

}
