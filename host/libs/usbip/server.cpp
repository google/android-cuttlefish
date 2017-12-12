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

#include "host/libs/usbip/server.h"

#include <glog/logging.h>
#include <netinet/in.h>
#include "common/libs/fs/shared_select.h"

using cvd::SharedFD;

namespace vadb {
namespace usbip {
Server::Server(const std::string& name, const DevicePool& devices)
    : name_{name}, device_pool_{devices} {}

bool Server::Init() { return CreateServerSocket(); }

// Open new listening server socket.
// Returns false, if listening socket could not be created.
bool Server::CreateServerSocket() {
  LOG(INFO) << "Starting server socket: " << name_;

  server_ = SharedFD::SocketLocalServer(name_.c_str(), true, SOCK_STREAM, 0700);
  if (!server_->IsOpen()) {
    LOG(ERROR) << "Could not create socket: " << server_->StrError();
    return false;
  }
  return true;
}

void Server::BeforeSelect(cvd::SharedFDSet* fd_read) const {
  fd_read->Set(server_);
  for (const auto& client : clients_) client.BeforeSelect(fd_read);
}

void Server::AfterSelect(const cvd::SharedFDSet& fd_read) {
  if (fd_read.IsSet(server_)) HandleIncomingConnection();

  for (auto iter = clients_.begin(); iter != clients_.end();) {
    if (!iter->AfterSelect(fd_read)) {
      // If client conversation failed, hang up.
      iter = clients_.erase(iter);
      continue;
    }
    ++iter;
  }
}

// Accept new USB/IP connection. Add it to client pool.
void Server::HandleIncomingConnection() {
  SharedFD client = SharedFD::Accept(*server_, nullptr, nullptr);
  if (!client->IsOpen()) {
    LOG(ERROR) << "Client connection failed: " << client->StrError();
    return;
  }

  clients_.emplace_back(device_pool_, client);
}
}  // namespace usbip
}  // namespace vadb
