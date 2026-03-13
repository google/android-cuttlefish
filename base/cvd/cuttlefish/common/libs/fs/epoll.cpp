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

#include "cuttlefish/common/libs/fs/epoll.h"

#include <sys/epoll.h>

#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<Epoll> Epoll::Create() {
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd == -1) {
    return CF_ERRNO("Failed to create epoll");
  }
  SharedFD shared{std::shared_ptr<FileInstance>(new FileInstance(fd, 0))};
  return Epoll(shared);
}

Epoll::Epoll() = default;

Epoll::Epoll(SharedFD epoll_fd) : epoll_fd_(epoll_fd) {}

Epoll::Epoll(Epoll&& other) {
  epoll_fd_ = std::move(other.epoll_fd_);
  watched_ = std::move(other.watched_);
}

Epoll& Epoll::operator=(Epoll&& other) {
  epoll_fd_ = std::move(other.epoll_fd_);
  watched_ = std::move(other.watched_);
  return *this;
}

Result<void> Epoll::Add(SharedFD fd, uint32_t events) {
  std::lock_guard lock(watched_mutex_);
  CF_EXPECT(epoll_fd_->IsOpen(), "Empty Epoll instance");

  if (watched_.count(fd) != 0) {
    return CF_ERRNO("Watched set already contains fd");
  }
  epoll_event event;
  event.events = events;
  event.data.fd = fd->fd_;
  int success = epoll_ctl(epoll_fd_->fd_, EPOLL_CTL_ADD, fd->fd_, &event);
  if (success != 0 && errno == EEXIST) {
    // We're already tracking this fd, don't drop it from the set.
    return CF_ERRNO("epoll_ctl: File descriptor was already present");
  } else if (success != 0) {
    return CF_ERRNO("epoll_ctl: Add failed");
  }
  watched_.insert(fd);
  return {};
}

Result<void> Epoll::AddOrModify(SharedFD fd, uint32_t events) {
  std::lock_guard lock(watched_mutex_);
  CF_EXPECT(epoll_fd_->IsOpen(), "Empty Epoll instance");

  epoll_event event;
  event.events = events;
  event.data.fd = fd->fd_;
  int operation = watched_.count(fd) == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
  int success = epoll_ctl(epoll_fd_->fd_, operation, fd->fd_, &event);
  if (success != 0) {
    std::string operation_str = operation == EPOLL_CTL_ADD ? "add" : "modify";
    return CF_ERRNO("epoll_ctl: Operation " << operation_str << " failed");
  }
  watched_.insert(fd);
  return {};
}

Result<void> Epoll::Modify(SharedFD fd, uint32_t events) {
  std::shared_lock lock(watched_mutex_);
  CF_EXPECT(epoll_fd_->IsOpen(), "Empty Epoll instance");

  if (watched_.count(fd) == 0) {
    return CF_ERR("Watched set did not contain fd");
  }
  epoll_event event;
  event.events = events;
  event.data.fd = fd->fd_;
  int success = epoll_ctl(epoll_fd_->fd_, EPOLL_CTL_MOD, fd->fd_, &event);
  if (success != 0) {
    return CF_ERRNO("epoll_ctl: Modify failed");
  }
  return {};
}

Result<void> Epoll::Delete(SharedFD fd) {
  std::lock_guard lock(watched_mutex_);
  CF_EXPECT(epoll_fd_->IsOpen(), "Empty Epoll instance");

  if (watched_.count(fd) == 0) {
    return CF_ERR("Watched set did not contain fd");
  }
  int success = epoll_ctl(epoll_fd_->fd_, EPOLL_CTL_DEL, fd->fd_, nullptr);
  if (success != 0) {
    return CF_ERRNO("epoll_ctl: Delete failed");
  }
  watched_.erase(fd);
  return {};
}

Result<std::optional<EpollEvent>> Epoll::Wait() {
  epoll_event event;
  int success;
  CF_EXPECT(epoll_fd_->IsOpen(), "Empty Epoll instance");
  success = TEMP_FAILURE_RETRY(epoll_wait(epoll_fd_->fd_, &event, 1, -1));
  if (success == -1) {
    return CF_ERRNO("epoll_wait failed");
  } else if (success == 0) {
    return {};
  } else if (success != 1) {
    return CF_ERR("epoll_wait returned an unexpected value");
  }
  EpollEvent ret;
  ret.events = event.events;
  std::shared_lock lock(watched_mutex_);
  for (const auto& watched : watched_) {
    if (watched->fd_ == event.data.fd) {
      ret.fd = watched;
      break;
    }
  }
  if (!ret.fd->IsOpen()) {
    // Couldn't find the matching SharedFD to the file descriptor. We probably
    // lost the race to lock watched_mutex_ against a delete call. Treat this
    // as a spurious wakeup.
    return {};
  }
  return ret;
}

}  // namespace cuttlefish
