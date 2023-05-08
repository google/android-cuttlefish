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

#include "common/libs/utils/socket2socket_proxy.h"

#include <poll.h>
#include <sys/socket.h>

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <android-base/logging.h>

namespace cuttlefish {
namespace {

void Forward(const std::string& label, SharedFD from, SharedFD to) {
  LOG(DEBUG) << label << ": Proxy thread started. Starting copying data";
  auto success = to->CopyAllFrom(*from);
  if (!success) {
    if (from->GetErrno()) {
      LOG(ERROR) << label << ": Error reading: " << from->StrError();
    }
    if (to->GetErrno()) {
      LOG(ERROR) << label << ": Error writing: " << to->StrError();
    }
  }
  to->Shutdown(SHUT_WR);
  LOG(DEBUG) << label << ": Proxy thread completed";
}

void SetupProxying(SharedFD client, SharedFD target) {
  LOG(DEBUG) << "Launching proxy threads";
  std::thread client2target(Forward, "c2t", client, target);
  std::thread target2client(Forward, "t2c", target, client);
  client2target.detach();
  target2client.detach();
}

}  // namespace

ProxyServer::ProxyServer(SharedFD server, std::function<SharedFD()> clients_factory)
    : stop_fd_(SharedFD::Event()) {

  if (!stop_fd_->IsOpen()) {
    LOG(FATAL) << "Failed to open eventfd: " << stop_fd_->StrError();
    return;
  }
  server_ = std::thread([&, server_fd = std::move(server),
                            clients_factory = std::move(clients_factory)]() {
    constexpr ssize_t SERVER = 0;
    constexpr ssize_t STOP = 1;

    std::vector<PollSharedFd> server_poll = {
      {.fd = server_fd, .events = POLLIN},
      {.fd = stop_fd_, .events = POLLIN}
    };

    while (server_fd->IsOpen()) {
      server_poll[SERVER].revents = 0;
      server_poll[STOP].revents = 0;

      const int poll_result = SharedFD::Poll(server_poll, -1);
      if (poll_result < 0) {
        LOG(ERROR) << "Failed to poll to wait for incoming connection";
        continue;
      }
      if (server_poll[STOP].revents & POLLIN) {
        // Stop fd is available to read, so we received a stop event
        // and must stop the thread
        break;
      }
      if (!(server_poll[SERVER].revents & POLLIN)) {
        continue;
      }

      // Server fd is available to read, so we can accept the
      // connection without blocking on that
      auto client = SharedFD::Accept(*server_fd);
      if (!client->IsOpen()) {
        LOG(ERROR) << "Failed to accept incoming connection: " << client->StrError();
        continue;
      }
      auto target = clients_factory();
      if (target->IsOpen()) {
        SetupProxying(client, target);
      } else {
        LOG(ERROR) << "Cannot connect to the target to setup proxying: " << target->StrError();
      }
      // The client will close when it goes out of scope here if the target
      // didn't open.
    }
  });
}

void ProxyServer::Join() {
  if (server_.joinable()) {
    server_.join();
  }
}

ProxyServer::~ProxyServer() {
  if (stop_fd_->EventfdWrite(1) != 0) {
    LOG(ERROR) << "Failed to stop proxy thread: " << stop_fd_->StrError();
  }
  Join();
}

void Proxy(SharedFD server, std::function<SharedFD()> conn_factory) {
  ProxyServer proxy(std::move(server), std::move(conn_factory));
  proxy.Join();
}

std::unique_ptr<ProxyServer> ProxyAsync(SharedFD server, std::function<SharedFD()> conn_factory) {
  return std::unique_ptr<ProxyServer>(
      new ProxyServer(std::move(server), std::move(conn_factory)));
}

}  // namespace cuttlefish
