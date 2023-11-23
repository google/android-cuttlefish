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

#include "cvd_server.pb.h"

#include "common/libs/fs/epoll.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/epoll_loop.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/logger.h"
// including "server_command/subcmd.h" causes cyclic dependency
#include "host/commands/cvd/server_command/host_tool_target_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/libs/web/build_api.h"

namespace cuttlefish {

struct ServerMainParam {
  SharedFD internal_server_fd;
  SharedFD carryover_client_fd;
  std::optional<SharedFD> memory_carryover_fd;
  std::optional<bool> acloud_translator_optout;
  std::unique_ptr<ServerLogger> server_logger;
  /* scoped logger that carries the stderr of the carried-over
   * client. The client may have called "cvd restart-server."
   *
   * The scoped_logger should expire just after AcceptCarryoverClient()
   */
  std::unique_ptr<ServerLogger::ScopedLogger> scoped_logger;
};
Result<int> CvdServerMain(ServerMainParam&& fds);

class CvdServer {
  // for server_logger_.
  // server_logger_ shouldn't be exposed to anything but CvdServerMain()
  friend Result<int> CvdServerMain(ServerMainParam&& fds);

 public:
  CvdServer(BuildApi&, EpollPool&, InstanceManager&, HostToolTargetManager&,
            ServerLogger&);
  ~CvdServer();

  Result<void> StartServer(SharedFD server);
  struct ExecParam {
    SharedFD new_exe;
    SharedFD carryover_client_fd;  // the client that called cvd restart-server
    std::optional<SharedFD>
        in_memory_data_fd;  // fd to carry over in-memory data
    bool verbose;
  };
  Result<void> Exec(const ExecParam&);
  Result<void> AcceptCarryoverClient(SharedFD client);
  void Stop();
  void Join();
  Result<void> InstanceDbFromJson(const std::string& json_string);

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

  SharedFD server_fd_;
  BuildApi& build_api_;
  EpollPool& epoll_pool_;
  InstanceManager& instance_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  ServerLogger& server_logger_;
  std::atomic_bool running_ = true;

  std::mutex ongoing_requests_mutex_;
  std::set<std::shared_ptr<OngoingRequest>> ongoing_requests_;
  // TODO(schuffelen): Move this thread pool to another class.
  std::mutex threads_mutex_;
  std::vector<std::thread> threads_;

  // translator optout
  std::atomic<bool> optout_;
};

// Read all contents from the file
Result<std::string> ReadAllFromMemFd(const SharedFD& mem_fd);

}  // namespace cuttlefish
