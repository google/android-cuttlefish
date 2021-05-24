/*
 * Copyright (C) 2020 The Android Open Source Project
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
#include <memory>
#include <vector>

#include "common/libs/concurrency/semaphore.h"
#include "common/libs/concurrency/thread_safe_queue.h"

namespace cuttlefish {
namespace confui {
template <typename T, typename Queue>
class Multiplexer {
 public:
  template <typename... Args>
  static std::unique_ptr<Queue> CreateQueue(Args&&... args) {
    auto raw_ptr = new Queue(std::forward<Args>(args)...);
    return std::unique_ptr<Queue>(raw_ptr);
  }

  Multiplexer() : sem_items_{0} {}

  int RegisterQueue(std::unique_ptr<Queue>&& queue) {
    const int id_to_return = queues_.size();
    queues_.push_back(std::move(queue));
    return id_to_return;
  }

  void Push(const int idx, T&& t) {
    CheckIdx(idx);
    queues_[idx]->Push(t);
    sem_items_.SemPost();
  }

  T Pop() {
    // the idx must have an item!
    // no waiting in fn()!
    SemWait();
    for (auto& q : queues_) {
      if (q->IsEmpty()) {
        continue;
      }
      return q->Pop();
    }
    CHECK(false) << "Multiplexer.Pop() should be able to return an item";
    // must not reach here
    return T{};
  }

 private:
  void SemWait() { sem_items_.SemWait(); }
  void CheckIdx(const int idx) {
    CHECK(idx >= 0 && idx < queues_.size()) << "queues_ array out of bound";
  }

  // total items across the queues
  Semaphore sem_items_;
  std::vector<std::unique_ptr<Queue>> queues_;
};
}  // end of namespace confui
}  // end of namespace cuttlefish
