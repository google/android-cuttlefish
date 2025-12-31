
//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/modem_simulator/client.h"

#include <android-base/strings.h>
#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"

namespace cuttlefish {

size_t ClientId::next_id_ = 0;

ClientId::ClientId() {
  id_ = next_id_;
  next_id_++;
}

bool ClientId::operator==(const ClientId& other) const {
  return id_ == other.id_;
}

Client::Client(SharedFD fd) : client_read_fd_(fd), client_write_fd_(fd) {}

Client::Client(SharedFD read, SharedFD write)
    : client_read_fd_(std::move(read)), client_write_fd_(std::move(write)) {}

Client::Client(SharedFD fd, ClientType client_type)
    : type(client_type), client_read_fd_(fd), client_write_fd_(fd) {}

Client::Client(SharedFD read, SharedFD write, ClientType client_type)
    : type(client_type),
      client_read_fd_(std::move(read)),
      client_write_fd_(std::move(write)) {}

bool Client::operator==(const Client& other) const {
  return client_read_fd_ == other.client_read_fd_ &&
         client_write_fd_ == other.client_write_fd_;
}

void Client::SendCommandResponse(std::string response) const {
  if (response.empty()) {
    VLOG(0) << "Invalid response, ignore!";
    return;
  }

  if (response.back() != '\r') {
    response += '\r';
  }
  VLOG(1) << " AT< " << response;

  std::lock_guard<std::mutex> lock(write_mutex);
  WriteAll(client_write_fd_, response);
}

void Client::SendCommandResponse(
    const std::vector<std::string>& responses) const {
  for (auto& response : responses) {
    SendCommandResponse(response);
  }
}

}  // namespace cuttlefish
