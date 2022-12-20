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

class Client {
 public:
  virtual SharedFD Start() = 0;
  virtual ~Client() = default;
};

class TcpClient : public Client {
 public:
  TcpClient(std::string host, int port);
  SharedFD Start() override;

 private:
  std::string host_;
  int port_;
  int last_failure_reason_ = 0;
};

class VsockClient : public Client {
 public:
  VsockClient(int id, int port);
  SharedFD Start() override;

 private:
  int id_;
  int port_;
  int last_failure_reason_ = 0;
};

}
}