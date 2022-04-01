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

#include <atomic>
#include <map>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/epoll.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/commands/cvd/epoll_loop.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_client.h"

namespace cuttlefish {

class CvdServerHandler {
 public:
  virtual ~CvdServerHandler() = default;

  virtual Result<bool> CanHandle(const RequestWithStdio&) const = 0;
  virtual Result<cvd::Response> Handle(const RequestWithStdio&) = 0;
  virtual Result<void> Interrupt() = 0;
};

class CvdServer {
 public:
  INJECT(CvdServer(EpollPool&, InstanceManager&));
  ~CvdServer();

  Result<void> StartServer(SharedFD server);
  void Stop();
  void Join();

 private:
  struct OngoingRequest {
    CvdServerHandler* handler;
    std::mutex mutex;
    std::thread::id thread_id;
  };

  Result<void> AcceptClient(EpollEvent);
  Result<void> HandleMessage(EpollEvent);
  Result<cvd::Response> HandleRequest(RequestWithStdio, SharedFD client);
  Result<void> BestEffortWakeup();

  EpollPool& epoll_pool_;
  InstanceManager& instance_manager_;
  std::atomic_bool running_ = true;

  std::mutex ongoing_requests_mutex_;
  std::set<std::shared_ptr<OngoingRequest>> ongoing_requests_;
  // TODO(schuffelen): Move this thread pool to another class.
  std::mutex threads_mutex_;
  std::vector<std::thread> threads_;
};

class CvdCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdCommandHandler(InstanceManager& instance_manager));

  Result<bool> CanHandle(const RequestWithStdio&) const override;
  Result<cvd::Response> Handle(const RequestWithStdio&) override;
  Result<void> Interrupt() override;

 private:
  InstanceManager& instance_manager_;
  std::optional<Subprocess> subprocess_;
  std::mutex interruptible_;
  bool interrupted_ = false;
};

fruit::Component<fruit::Required<InstanceManager>> cvdCommandComponent();
fruit::Component<fruit::Required<CvdServer, InstanceManager>>
cvdShutdownComponent();
fruit::Component<> cvdVersionComponent();
fruit::Component<fruit::Required<CvdCommandHandler>> AcloudCommandComponent();

struct CommandInvocation {
  std::string command;
  std::vector<std::string> arguments;
};

CommandInvocation ParseInvocation(const cvd::Request& request);

Result<int> CvdServerMain(SharedFD server_fd);

}  // namespace cuttlefish
