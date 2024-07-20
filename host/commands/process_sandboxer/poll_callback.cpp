/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/process_sandboxer/poll_callback.h"

#include <poll.h>

#include <functional>
#include <vector>

#include <absl/log/log.h>
#include <absl/status/status.h>

using absl::Status;

namespace cuttlefish {
namespace process_sandboxer {

void PollCallback::Add(int fd, std::function<Status(short)> cb) {
  pollfds_.emplace_back(pollfd{
      .fd = fd,
      .events = POLLIN,
  });
  callbacks_.emplace_back(std::move(cb));
}

Status PollCallback::Poll() {
  int poll_ret = poll(pollfds_.data(), pollfds_.size(), 0);
  if (poll_ret < 0) {
    return Status(absl::ErrnoToStatusCode(errno), "`poll` failed");
  }

  VLOG(2) << "`poll` returned " << poll_ret;

  for (size_t i = 0; i < pollfds_.size() && i < callbacks_.size(); i++) {
    const auto& poll_fd = pollfds_[i];
    if (poll_fd.revents == 0) {
      continue;
    }
    auto status = callbacks_[i](poll_fd.revents);
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

}  // namespace process_sandboxer
}  // namespace cuttlefish
