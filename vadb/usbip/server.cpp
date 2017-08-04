/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "host/vadb/usbip/server.h"

#include <glog/logging.h>
#include <netinet/in.h>

#include "common/libs/fs/shared_select.h"

using avd::SharedFD;

namespace vadb {
namespace usbip {
namespace {
// USB-IP server port. USBIP will attempt to connect to this server to attach
// new virtual USB devices to host.
static constexpr int kServerPort = 3240;
}  // namespace

Server::Server(const DevicePool &devices) : device_pool_(devices) {}

bool Server::Init() { return CreateServerSocket(); }

// Open new listening server socket.
// Returns false, if listening socket could not be created.
bool Server::CreateServerSocket() {
  LOG(INFO) << "Starting server socket on port " << kServerPort;
  server_ = SharedFD::Socket(PF_INET6, SOCK_STREAM, 0);
  if (!server_->IsOpen()) {
    LOG(ERROR) << "Could not create socket: " << server_->StrError();
    return false;
  }

  int n = 1;
  if (server_->SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) == -1) {
    LOG(ERROR) << "SetSockOpt failed " << server_->StrError();
    return false;
  }

  struct sockaddr_in6 addr = {
      AF_INET6, htons(kServerPort), 0, in6addr_loopback, 0,
  };

  if (server_->Bind((struct sockaddr *)&addr, sizeof(addr)) == -1) {
    LOG(ERROR) << "Could not bind socket: " << server_->StrError();
    return false;
  }

  if (server_->Listen(1) == -1) {
    LOG(ERROR) << "Could not start listening: " << server_->StrError();
    return false;
  }

  return true;
}

// Serve incoming USB/IP connections.
void Server::Serve() {
  LOG(INFO) << "Serving USB/IP connections.";
  while (true) {
    avd::SharedFDSet fd_read;
    fd_read.Set(server_);
    for (const auto &client : clients_) fd_read.Set(client.fd());

    int ret = avd::Select(&fd_read, nullptr, nullptr, nullptr);
    if (ret <= 0) continue;

    if (fd_read.IsSet(server_)) HandleIncomingConnection();

    for (auto iter = clients_.begin(); iter != clients_.end();) {
      if (fd_read.IsSet(iter->fd())) {
        // If client conversation failed, hang up.
        if (!iter->HandleIncomingMessage()) {
          iter = clients_.erase(iter);
          continue;
        }
      }
      ++iter;
    }
  }
  LOG(INFO) << "Server exiting.";
}

// Accept new USB/IP connection. Add it to client pool.
void Server::HandleIncomingConnection() {
  SharedFD client = SharedFD::Accept(*server_, nullptr, nullptr);
  if (!client->IsOpen()) {
    LOG(ERROR) << "Client connection failed: " << client->StrError();
    return;
  }

  clients_.emplace_back(device_pool_, client);
  clients_.back().SetAttached(init_attached_state_);
}
}  // namespace usbip
}  // namespace vadb
