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
#include "host/commands/process_sandboxer/unique_fd.h"

#include <unistd.h>

#include <absl/log/log.h>

namespace cuttlefish {
namespace process_sandboxer {

UniqueFd::UniqueFd(int fd) : fd_(fd) {}

UniqueFd::UniqueFd(UniqueFd&& other) { std::swap(fd_, other.fd_); }

UniqueFd::~UniqueFd() { Close(); }

UniqueFd& UniqueFd::operator=(UniqueFd&& other) {
  Close();
  std::swap(fd_, other.fd_);
  return *this;
}

int UniqueFd::Get() const { return fd_; }

int UniqueFd::Release() {
  int ret = -1;
  std::swap(ret, fd_);
  return ret;
}

void UniqueFd::Reset(int fd) {
  Close();
  fd_ = fd;
}

void UniqueFd::Close() {
  if (fd_ > 0 && close(fd_) < 0) {
    PLOG(ERROR) << "Failed to close fd " << fd_;
  }
  fd_ = -1;
}

}  // namespace process_sandboxer
}  // namespace cuttlefish
