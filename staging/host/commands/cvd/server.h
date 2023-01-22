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
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/commands/cvd/epoll_loop.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/logger.h"
// including "server_command/subcmd.h" causes cyclic dependency
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/libs/config/inject.h"
#include "host/libs/web/build_api.h"

namespace cuttlefish {

class CvdServer {
 public:
  INJECT(CvdServer(BuildApi&, EpollPool&, InstanceManager&, ServerLogger&));
  ~CvdServer();

  Result<void> StartServer(SharedFD server);
  Result<void> Exec(SharedFD new_exe, SharedFD client);
  Result<void> AcceptCarryoverClient(SharedFD client);
  void Stop();
  void Join();

 private:
  struct OngoingRequest {
    CvdServerHandler* handler;
    std::mutex mutex;
    std::thread::id thread_id;
  };

  /* this has to be static due to the way fruit includes components */
  static fruit::Component<> RequestComponent(CvdServer*);

  Result<void> AcceptClient(EpollEvent);
  Result<void> HandleMessage(EpollEvent);
  Result<cvd::Response> HandleRequest(RequestWithStdio, SharedFD client);
  Result<void> BestEffortWakeup();

  SharedFD server_fd_;
  BuildApi& build_api_;
  EpollPool& epoll_pool_;
  InstanceManager& instance_manager_;
  ServerLogger& server_logger_;
  std::atomic_bool running_ = true;

  std::mutex ongoing_requests_mutex_;
  std::set<std::shared_ptr<OngoingRequest>> ongoing_requests_;
  // TODO(schuffelen): Move this thread pool to another class.
  std::mutex threads_mutex_;
  std::vector<std::thread> threads_;
};

Result<CvdServerHandler*> RequestHandler(
    const RequestWithStdio& request,
    const std::vector<CvdServerHandler*>& handlers);

Result<int> CvdServerMain(SharedFD server_fd, SharedFD carryover_client);

}  // namespace cuttlefish
