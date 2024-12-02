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

#include "host/libs/input_connector/input_connection.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace {
class ServerInputConnection : public InputConnection {
 public:
  ServerInputConnection(SharedFD server);

  Result<void> WriteEvents(const void* data, size_t len) override;

 private:
  SharedFD server_;
  SharedFD client_;
  std::mutex client_mtx_;
  std::thread monitor_;

  void MonitorLoop();
};

ServerInputConnection::ServerInputConnection(SharedFD server)
    : server_(server), monitor_(std::thread([this]() { MonitorLoop(); })) {}

void ServerInputConnection::MonitorLoop() {
  for (;;) {
    client_ = SharedFD::Accept(*server_);
    if (!client_->IsOpen()) {
      LOG(ERROR) << "Failed to accept on input socket: " << client_->StrError();
      continue;
    }
    do {
      // Keep reading from the fd to detect when it closes.
      char buf[128];
      auto res = client_->Read(buf, sizeof(buf));
      if (res < 0) {
        LOG(ERROR) << "Failed to read from input client: "
                   << client_->StrError();
      } else if (res > 0) {
        LOG(VERBOSE) << "Received " << res << " bytes on input socket";
      } else {
        std::lock_guard<std::mutex> lock(client_mtx_);
        client_->Close();
      }
    } while (client_->IsOpen());
  }
}

Result<void> ServerInputConnection::WriteEvents(const void* data, size_t len) {
  std::lock_guard<std::mutex> lock(client_mtx_);
  CF_EXPECT(client_->IsOpen(), "No input client connected");
  auto res = WriteAll(client_, reinterpret_cast<const char*>(data), len);
  CF_EXPECT(res == len, "Failed to write entire event buffer: wrote "
                            << res << " of " << len << "bytes");
  return {};
}

}  // namespace

std::unique_ptr<InputConnection> NewServerInputConnection(SharedFD server_fd) {
  return std::unique_ptr<InputConnection>(new ServerInputConnection(server_fd));
}

}  // namespace cuttlefish
