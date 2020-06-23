#pragma once

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

#include "common/libs/fs/shared_fd.h"

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace cuttlefish {
using Message = std::vector<std::uint8_t>;

class ServerSocket;

// Recv and Send wait until all data has been received or sent.
// Send is thread safe in this regard, Recv is not.
class ClientSocket {
 public:
  ClientSocket(ClientSocket&& other) : fd_{other.fd_} {}

  ClientSocket& operator=(ClientSocket&& other) {
    fd_ = other.fd_;
    return *this;
  }

  ClientSocket(int port);

  ClientSocket(const ClientSocket&) = delete;
  ClientSocket& operator=(const ClientSocket&) = delete;

  Message Recv(std::size_t length);
  // RecvAny will receive whatever is available.
  // An empty message returned indicates error or close.
  Message RecvAny(std::size_t length);
  // Sends are called with MSG_NOSIGNAL to suppress SIGPIPE
  ssize_t SendNoSignal(const std::uint8_t* data, std::size_t size);
  ssize_t SendNoSignal(const Message& message);

  template <std::size_t N>
  ssize_t SendNoSignal(const std::uint8_t (&data)[N]) {
    return SendNoSignal(data, N);
  }

  bool closed() const;

 private:
  friend ServerSocket;
  explicit ClientSocket(cuttlefish::SharedFD fd) : fd_(fd) {}

  cuttlefish::SharedFD fd_;
  bool other_side_closed_{};
  mutable std::mutex closed_lock_;
  std::mutex send_lock_;
};

class ServerSocket {
 public:
  explicit ServerSocket(int port);

  ServerSocket(const ServerSocket&) = delete;
  ServerSocket& operator=(const ServerSocket&) = delete;

  ClientSocket Accept();

 private:
  cuttlefish::SharedFD fd_;
};

void AppendInNetworkByteOrder(Message* msg, const std::uint8_t b);
void AppendInNetworkByteOrder(Message* msg, const std::uint16_t s);
void AppendInNetworkByteOrder(Message* msg, const std::uint32_t w);
void AppendInNetworkByteOrder(Message* msg, const std::int32_t w);
void AppendInNetworkByteOrder(Message* msg, const std::string& str);

inline void AppendToMessage(Message*) {}

template <typename T, typename... Ts>
void AppendToMessage(Message* msg, T v, Ts... vals) {
  AppendInNetworkByteOrder(msg, v);
  AppendToMessage(msg, vals...);
}

template <typename... Ts>
Message CreateMessage(Ts... vals) {
  Message m;
  AppendToMessage(&m, vals...);
  return m;
}

}  // namespace cuttlefish
