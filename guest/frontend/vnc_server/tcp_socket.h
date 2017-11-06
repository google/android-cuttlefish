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

#include "vnc_utils.h"

#include <mutex>
#include <cstdint>
#include <cstddef>

#include <unistd.h>

namespace cvd {
namespace vnc {

class ServerSocket;

// Recv and Send wait until all data has been received or sent.
// Send is thread safe in this regard, Recv is not.
class ClientSocket {
 public:
  ClientSocket(ClientSocket&& other) : fd_{other.fd_} {
    other.fd_ = -1;
  }

  ClientSocket& operator=(ClientSocket&& other) {
    if (fd_ >= 0) {
      close(fd_);
    }
    fd_ = other.fd_;
    other.fd_ = -1;
    return *this;
  }

  ClientSocket(const ClientSocket&) = delete;
  ClientSocket& operator=(const ClientSocket&) = delete;

  ~ClientSocket() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  Message Recv(std::size_t length);
  ssize_t Send(const std::uint8_t* data, std::size_t size);
  ssize_t Send(const Message& message);

  template <std::size_t N>
  ssize_t Send(const std::uint8_t (&data)[N]) {
    return Send(data, N);
  }

  bool closed() const {
    return other_side_closed_;
  }

 private:
  friend ServerSocket;
  explicit ClientSocket(int fd) : fd_(fd) {}

  int fd_ = -1;
  bool other_side_closed_{};
  std::mutex send_lock_;
};

class ServerSocket {
 public:
  explicit ServerSocket(int port);

  ServerSocket(const ServerSocket&) = delete;
  ServerSocket& operator=(const ServerSocket&) = delete;

  ~ServerSocket() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  ClientSocket Accept();

 private:
  int fd_ = -1;
};

}  // namespace vnc
}  // namespace cvd
