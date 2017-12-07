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

#include "host/libs/vadb/virtual_adb_server.h"

namespace vadb {

bool VirtualADBServer::Init() {
  LOG(INFO) << "Starting server socket: " << name_;

  server_ =
      cvd::SharedFD::SocketLocalServer(name_.c_str(), false, SOCK_STREAM, 0666);
  if (!server_->IsOpen()) {
    LOG(ERROR) << "Could not create socket: " << server_->StrError();
    return false;
  }
  return true;
}

void VirtualADBServer::BeforeSelect(cvd::SharedFDSet* fd_read) const {
  fd_read->Set(server_);
  for (const auto& client : clients_) client.BeforeSelect(fd_read);
}

void VirtualADBServer::AfterSelect(const cvd::SharedFDSet& fd_read) {
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

// Accept new QEmu connection. Add it to client pool.
// Typically we will have no more than one QEmu connection, but the nature
// of server requires proper handling nonetheless.
void VirtualADBServer::HandleIncomingConnection() {
  cvd::SharedFD client = cvd::SharedFD::Accept(*server_, nullptr, nullptr);
  if (!client->IsOpen()) {
    LOG(ERROR) << "Client connection failed: " << client->StrError();
    return;
  }

  clients_.emplace_back(&pool_, client, usbip_name_);
}

}  // namespace vadb
