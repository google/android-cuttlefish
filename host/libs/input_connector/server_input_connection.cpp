/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/libs/input_connector/server_input_connection.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"

#include "host/libs/input_connector/input_connection.h"
#include "host/libs/input_connector/full_duplex_fd_input_connection.h"

namespace cuttlefish {
ServerInputConnection::ServerInputConnection(SharedFD server)
    : server_(server), monitor_(std::thread([this]() { MonitorLoop(); })) {}

void ServerInputConnection::MonitorLoop() {
  for (;;) {
    SharedFD client = SharedFD::Accept(*server_);
    if (!client->IsOpen()) {
      LOG(ERROR) << "Failed to accept on input socket: " << client->StrError();
      continue;
    }
    {
      std::lock_guard lock(client_mtx_);
      client_ = std::make_unique<FullDuplexFdInputConnection>(client);
    }
    do {
      // Keep reading from the client fd to detect when it closes.
      char buf[128];
      auto res = client->Read(buf, sizeof(buf));
      if (res < 0) {
        LOG(ERROR) << "Failed to read from input client: "
                   << client->StrError();
      } else if (res > 0) {
        LOG(VERBOSE) << "Received " << res << " bytes on input socket";
      } else {
        // The other side of the connection was closed.
        std::lock_guard<std::mutex> lock(client_mtx_);
        client_.reset();
      }
      // No need to lock the mutex here, we're only reading the value of the
      // pointer which is only ever set in this thread.
    } while (client_);
  }
}

Result<void> ServerInputConnection::WriteEvents(const void* data, size_t len) {
  std::lock_guard<std::mutex> lock(client_mtx_);
  CF_EXPECT(client_ != nullptr, "No input client connected");
  CF_EXPECT(client_->WriteEvents(data, len));
  return {};
}

}  // namespace cuttlefish
