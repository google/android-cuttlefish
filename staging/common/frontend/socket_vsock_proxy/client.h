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

#include <chrono>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace socket_proxy {

class Client {
 public:
  virtual SharedFD Start() = 0;
  virtual std::string Describe() const = 0;
  virtual ~Client() = default;
};

class TcpClient : public Client {
 public:
  TcpClient(std::string host, int port, std::chrono::seconds timeout = std::chrono::seconds(0));
  SharedFD Start() override;
  std::string Describe() const override;

 private:
  std::string host_;
  int port_;
  std::chrono::seconds timeout_;
  int last_failure_reason_ = 0;
};

class VsockClient : public Client {
 public:
  VsockClient(int id, int port, bool vhost_user_vsock);
  SharedFD Start() override;
  std::string Describe() const override;

 private:
  int id_;
  int port_;
  bool vhost_user_vsock_;
  int last_failure_reason_ = 0;
};

}
}