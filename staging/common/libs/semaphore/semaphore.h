/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace cuttlefish {
/**
 * An ad-hoc semaphore used to track the number of items in all queue
 */
class Semaphore {
 public:
  Semaphore(const int init_val = 0) : count_{init_val} {}

  // called by the threads that consumes all of the multiple queues
  void SemWait() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() -> bool { return this->count_ > 0; });
    --count_;
  }

  // called by each producer thread effectively, whenever an item is added
  void SemPost() {
    std::unique_lock<std::mutex> lock(mtx_);
    if (++count_ > 0) {
      cv_.notify_all();
    }
  }

  void SemWaitItem() { SemWait(); }

  // Only called by the producers
  void SemPostItem() { SemPost(); }

 private:
  std::mutex mtx_;
  std::condition_variable cv_;
  int count_;
};

}  // namespace cuttlefish
