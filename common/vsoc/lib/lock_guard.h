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

namespace vsoc {

/*
 * Implements std::lock_guard like functionality for the vsoc locks.
 */

template <typename Lock>
class LockGuard {
 public:
  explicit LockGuard(Lock* lock) : lock_(lock) {
    lock_->Lock();
  }

  ~LockGuard() {
    lock_->Unlock();
  }

  LockGuard(const LockGuard<Lock>&) = delete;
  LockGuard<Lock>& operator=(const LockGuard<Lock>&) = delete;

 private:
  Lock* lock_;
};

} // namespace vsoc
