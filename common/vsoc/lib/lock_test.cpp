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

#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "common/vsoc/lib/region_view.h"

#ifdef ANDROID
using MyLock = vsoc::layout::GuestLock;
#else
using MyLock = vsoc::layout::HostLock;
#endif

class SimpleLocker {
 public:
  enum State {
    BEFORE_EXECUTION,
    BEFORE_LOCK,
    IN_CRITICAL_SECTION,
    DONE,
    JOINED
  };

  explicit SimpleLocker(MyLock* lock)
      : lock_(lock), thread_(&SimpleLocker::Work, this) {}

  void Work() {
    state_ = BEFORE_LOCK;
    lock_->Lock();
    state_ = IN_CRITICAL_SECTION;
    InCriticalSection();
    lock_->Unlock();
    state_ = DONE;
  }

  void InCriticalSection() {}

  void Join() {
    thread_.join();
    state_ = JOINED;
  }

 protected:
  MyLock* lock_;
  volatile State state_{BEFORE_EXECUTION};
  std::thread thread_;
};

TEST(LockTest, Basic) {
  // In production regions are always 0 filled on allocation. That's not
  // true on the stack, so initialize the lock when we declare it.
  MyLock lock{};
  SimpleLocker a(&lock);
  SimpleLocker b(&lock);
  a.Join();
  b.Join();
}
