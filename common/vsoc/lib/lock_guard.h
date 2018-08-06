#pragma once

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

#include "common/vsoc/shm/lock.h"

namespace vsoc {

class RegionView;

/*
 * Implements std::lock_guard like functionality for the vsoc locks.
 */

template <typename Lock>
class LockGuard {
 public:
  explicit LockGuard(Lock* lock) : lock_(lock) {
    lock_->Lock();
  }

  LockGuard(LockGuard&& o) noexcept {
    lock_ = o.lock_;
    o.lock_ = nullptr;
  }

  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

  ~LockGuard() {
    if (lock_) {
      lock_->Unlock();
    }
  }

 private:
  Lock* lock_;
};

template <>
class LockGuard<::vsoc::layout::GuestAndHostLock> {
  using Lock = ::vsoc::layout::GuestAndHostLock;

 public:
  LockGuard(Lock* lock, RegionView* region) : lock_(lock), region_(region) {
    lock_->Lock(region_);
  }

  LockGuard(LockGuard&& o) noexcept {
    lock_ = o.lock_;
    o.lock_ = nullptr;
    region_ = o.region_;
    o.region_ = nullptr;
  }

  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

  ~LockGuard() {
    if (lock_) {
      lock_->Unlock(region_);
    }
  }

 private:
  Lock* lock_;
  RegionView* region_;
};

}  // namespace vsoc
