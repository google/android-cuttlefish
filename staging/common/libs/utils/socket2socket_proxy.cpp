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
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <android-base/logging.h>

namespace cuttlefish {
namespace {

class ProxyPair {
 public:
  ProxyPair()
      : stop_fd_(SharedFD::Event()) {
    if (!stop_fd_->IsOpen()) {
      LOG(FATAL) << "Failed to open eventfd: " << stop_fd_->StrError();
      return;
    }
  }

  ProxyPair(ProxyPair&& other)
      : started_(other.started_),
        stop_fd_(std::move(other.stop_fd_)),
        c2t_running_(other.c2t_running_.load()),
        t2c_running_(other.t2c_running_.load()) {
    if (other.started_) {
      LOG(FATAL) << "ProxyPair cannot be moved after Start() being executed";
    }
  }

  ~ProxyPair() {
    if (stop_fd_->IsOpen() && stop_fd_->EventfdWrite(1) != 0) {
      LOG(ERROR) << "Failed to stop proxy thread: " << stop_fd_->StrError();
    }
    if (c2t_.joinable()) {
      c2t_.join();
    }
    if (t2c_.joinable()) {
      t2c_.join();
    }
  }

  void Start(SharedFD from, SharedFD to) {
    if (started_) {
      LOG(FATAL) << "ProxyPair cannot be started second time";
    }
    started_ = true;
    c2t_running_ = true;
    t2c_running_ = true;
    c2t_ = std::thread(&ProxyPair::Forward, this, "c2t", from, to, stop_fd_,
                       std::ref(c2t_running_));
    t2c_ = std::thread(&ProxyPair::Forward, this, "t2c", to, from, stop_fd_,
                       std::ref(t2c_running_));
  }

  bool Running() {
    return c2t_running_ || t2c_running_;
  }

 private:
  void Forward(const std::string& label, SharedFD from, SharedFD to,
               SharedFD stop, std::atomic<bool>& running) {
    LOG(DEBUG) << label << ": Proxy thread started. Starting copying data";
    auto success = to->CopyAllFrom(*from, &(*stop));
    if (!success) {
      if (from->GetErrno()) {
        LOG(ERROR) << label << ": Error reading: " << from->StrError();
      }
      if (to->GetErrno()) {
        LOG(ERROR) << label << ": Error writing: " << to->StrError();
      }
    }
    to->Shutdown(SHUT_WR);
    running = false;
    LOG(DEBUG) << label << ": Proxy thread completed";
  }

  bool started_;
  SharedFD stop_fd_;
  std::atomic<bool> c2t_running_;
  std::atomic<bool> t2c_running_;
  std::thread c2t_;
  std::thread t2c_;
};

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
    std::list<ProxyPair> watched;

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
        LOG(DEBUG) << "Launching proxy threads";
        watched.push_back(ProxyPair());
        watched.back().Start(client, target);
        LOG(DEBUG) << "Proxy is launched. Amount of currently tracked proxy pairs: "
                   << watched.size();
      } else {
        LOG(ERROR) << "Cannot connect to the target to setup proxying: " << target->StrError();
      }
      // Unwatch completed proxy pairs
      watched.remove_if([](ProxyPair& proxy) { return !proxy.Running(); });
    }

    // Making sure all launched proxy pairs are finished by triggering their destructor
    LOG(DEBUG) << "Waiting for proxy threads to turn down";
    watched.clear();
    LOG(DEBUG) << "Proxy threads are successfully turned down";
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
  return std::make_unique<ProxyServer>(std::move(server), std::move(conn_factory));
}

}  // namespace cuttlefish
