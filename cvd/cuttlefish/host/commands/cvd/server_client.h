/*
 * Copyright (C) 2022 The Android Open Source Project
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

#pragma once

#include <sys/socket.h>

#include <memory>
#include <optional>
#include <vector>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

class RequestWithStdio {
 public:
  RequestWithStdio(cvd::Request, std::vector<SharedFD>, std::optional<ucred>);

  const cvd::Request& Message() const;
  SharedFD In() const;
  SharedFD Out() const;
  SharedFD Err() const;
  std::optional<SharedFD> Extra() const;
  std::optional<ucred> Credentials() const;

 private:
  cvd::Request message_;
  std::vector<SharedFD> fds_;
  std::optional<ucred> creds_;
};

class ClientMessageQueue {
 public:
  static Result<ClientMessageQueue> Create(SharedFD client);
  ClientMessageQueue(ClientMessageQueue&&);
  ~ClientMessageQueue();
  ClientMessageQueue& operator=(ClientMessageQueue&&);

  Result<RequestWithStdio> WaitForRequest();
  Result<void> PostResponse(const cvd::Response& response);
  Result<void> Stop();
  void Join();

 private:
  class Internal;

  std::unique_ptr<Internal> internal_;

  ClientMessageQueue(std::unique_ptr<Internal>);
};

}  // namespace cuttlefish
