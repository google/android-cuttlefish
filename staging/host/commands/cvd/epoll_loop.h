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

#include <functional>
#include <map>
#include <mutex>

#include "common/libs/fs/epoll.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

#pragma once

namespace cuttlefish {

using EpollCallback = std::function<Result<void>(EpollEvent)>;

class EpollPool {
 public:
  EpollPool();

  /**
   * The `callback` function will be invoked with an EpollEvent containing `fd`
   * and a subset of the bits in `events` matching which events were actually
   * observed. The callback is invoked exactly once (enforced via EPOLLONESHOT)
   * and must be re-`Register`ed to receive events again. This can be done
   * in the callback implementation. Callbacks are invoked by callers of the
   * `HandleEvent` function, and any errors produced by the callback function
   * will manifest there. Callbacks that return errors will not be automatically
   * re-registered.
   */
  Result<void> Register(SharedFD fd, uint32_t events, EpollCallback callback);
  Result<void> HandleEvent();
  Result<void> Remove(SharedFD fd);

 private:
  Epoll epoll_;
  std::mutex callbacks_mutex_;
  std::map<SharedFD, EpollCallback> callbacks_;
};

}  // namespace cuttlefish
