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

namespace cvd {
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
  Message RecvAny(size_t length);
  ssize_t Send(const std::uint8_t* data, std::size_t size);
  ssize_t Send(const Message& message);

  template <std::size_t N>
  ssize_t Send(const std::uint8_t (&data)[N]) {
    return Send(data, N);
  }

  bool closed() const;

 private:
  friend ServerSocket;
  explicit ClientSocket(cvd::SharedFD fd) : fd_(fd) {}

  cvd::SharedFD fd_;
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
  cvd::SharedFD fd_;
};

}  // namespace cvd
