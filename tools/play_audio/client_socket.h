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
#ifndef CFP_CLIENT_SOCKET_
#define CFP_CLIENT_SOCKET_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace cfp {

class ClientSocket {
 public:
  ClientSocket(std::uint16_t port);
  ~ClientSocket();

  ClientSocket(ClientSocket&& other);
  ClientSocket& operator=(ClientSocket&& other);

  ClientSocket(const ClientSocket&) = delete;
  ClientSocket& operator=(const ClientSocket&) = delete;

  bool valid() const { return socket_fd_ >= 0; }

  std::vector<unsigned char> RecvAll(ssize_t count);

  std::uint16_t RecvUInt16();
  std::uint32_t RecvUInt32();

 private:
  void close();
  int socket_fd_ = -1;
};

}  // namespace cfp

#endif
