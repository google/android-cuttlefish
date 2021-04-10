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
class Semaphore {
 public:
  Semaphore(const unsigned int init_val = 0, const unsigned int cap = 30000)
      : count_{init_val}, capacity_{cap} {}

  void SemWait() {
    std::unique_lock<std::mutex> lock(mtx_);
    resoure_cv_.wait(lock, [this]() -> bool { return count_ > 0; });
    --count_;
    room_cv_.notify_one();
  }

  void SemPost() {
    std::unique_lock<std::mutex> lock(mtx_);
    room_cv_.wait(lock, [this]() -> bool { return count_ <= capacity_; });
    ++count_;
    resoure_cv_.notify_one();
  }

 private:
  std::mutex mtx_;
  std::condition_variable resoure_cv_;
  std::condition_variable room_cv_;
  unsigned int count_;
  const unsigned int capacity_;  // inclusive upper limit
};

}  // namespace cuttlefish
