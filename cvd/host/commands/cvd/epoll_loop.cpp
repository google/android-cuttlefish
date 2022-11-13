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

#include "host/commands/cvd/epoll_loop.h"

#include <android-base/errors.h>

#include "common/libs/fs/epoll.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

EpollPool::EpollPool(Epoll epoll) : epoll_(std::move(epoll)) {}

EpollPool::EpollPool(EpollPool&& other) {
  std::unique_lock own_lock(instance_mutex_, std::defer_lock);
  std::unique_lock other_lock(other.instance_mutex_, std::defer_lock);
  std::unique_lock own_cb_lock(callbacks_mutex_, std::defer_lock);
  std::unique_lock other_cb_lock(other.callbacks_mutex_, std::defer_lock);
  std::lock(own_lock, other_lock, own_cb_lock, other_cb_lock);
  epoll_ = std::move(other.epoll_);
  callbacks_ = std::move(other.callbacks_);
}

EpollPool& EpollPool::operator=(EpollPool&& other) {
  std::unique_lock own_lock(instance_mutex_, std::defer_lock);
  std::unique_lock other_lock(other.instance_mutex_, std::defer_lock);
  std::unique_lock own_cb_lock(callbacks_mutex_, std::defer_lock);
  std::unique_lock other_cb_lock(other.callbacks_mutex_, std::defer_lock);
  std::lock(own_lock, other_lock, own_cb_lock, other_cb_lock);
  epoll_ = std::move(other.epoll_);
  callbacks_ = std::move(other.callbacks_);

  return *this;
}

Result<void> EpollPool::Register(SharedFD fd, uint32_t events,
                                 EpollCallback callback) {
  std::shared_lock instance_lock(instance_mutex_, std::defer_lock);
  std::unique_lock callbacks_lock(callbacks_mutex_, std::defer_lock);
  std::lock(instance_lock, callbacks_lock);
  CF_EXPECT(!Contains(callbacks_, fd), "Already have a callback created");
  CF_EXPECT(epoll_.AddOrModify(fd, events | EPOLLONESHOT));
  callbacks_[fd] = std::move(callback);
  return {};
}

Result<void> EpollPool::HandleEvent() {
  auto event = CF_EXPECT(epoll_.Wait());
  if (!event) {
    return {};
  }
  EpollCallback callback;
  {
    std::lock_guard lock(callbacks_mutex_);
    auto it = callbacks_.find(event->fd);
    CF_EXPECT(it != callbacks_.end(), "Could not find event callback");
    callback = std::move(it->second);
    callbacks_.erase(it);
  }
  CF_EXPECT(callback(*event));
  return {};
}

Result<void> EpollPool::Remove(SharedFD fd) {
  std::shared_lock instance_lock(instance_mutex_, std::defer_lock);
  std::unique_lock callbacks_lock(callbacks_mutex_, std::defer_lock);
  std::lock(instance_lock, callbacks_lock);
  CF_EXPECT(epoll_.Delete(fd), "No callback registered with epoll");
  callbacks_.erase(fd);
  return {};
}

fruit::Component<EpollPool> EpollLoopComponent() {
  return fruit::createComponent().registerProvider([]() -> EpollPool {
    auto epoll = Epoll::Create();
    if (epoll.ok()) {
      return EpollPool(std::move(*epoll));
    }
    LOG(ERROR) << epoll.error().Message();
    LOG(DEBUG) << epoll.error().Trace();
    abort();
  });
}

}  // namespace cuttlefish
