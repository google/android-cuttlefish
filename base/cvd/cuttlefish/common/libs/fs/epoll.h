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

#pragma once

#include <sys/epoll.h>

#include <optional>
#include <set>
#include <shared_mutex>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct EpollEvent {
  SharedFD fd;
  uint32_t events;
};

class Epoll {
 public:
  static Result<Epoll> Create();
  Epoll();  // Invalid instance
  Epoll(Epoll&&);
  Epoll& operator=(Epoll&&);

  Result<void> Add(SharedFD fd, uint32_t events);
  Result<void> Modify(SharedFD fd, uint32_t events);
  Result<void> AddOrModify(SharedFD fd, uint32_t events);
  Result<void> Delete(SharedFD fd);
  Result<std::optional<EpollEvent>> Wait();

 private:
  Epoll(SharedFD);

  SharedFD epoll_fd_;
  /**
   * This read-write mutex is read-locked when interacting with it as a const
   * std::set, and write-locked when interacting with it as a std::set.
   */
  std::shared_mutex watched_mutex_;
  std::set<SharedFD> watched_;
};

}  // namespace cuttlefish
