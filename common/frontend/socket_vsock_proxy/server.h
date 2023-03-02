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

#pragma once

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace socket_proxy {

class Server {
 public:
  virtual SharedFD Start() = 0;
  virtual ~Server() = default;
};

class TcpServer : public Server {
 public:
  TcpServer(int port);
  SharedFD Start() override;

 private:
  int port_;
};

class VsockServer : public Server {
 public:
  VsockServer(int port);
  SharedFD Start() override;

 private:
  int port_;
};

class DupServer : public Server {
 public:
  DupServer(int fd);
  SharedFD Start() override;

 private:
  SharedFD fd_;
};

}
}