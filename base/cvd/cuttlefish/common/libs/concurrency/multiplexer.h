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
#include <functional>
#include <memory>
#include <vector>

#include "common/libs/concurrency/semaphore.h"
#include "common/libs/concurrency/thread_safe_queue.h"

namespace cuttlefish {
template <typename T, typename Queue>
class Multiplexer {
 public:
  using QueuePtr = std::unique_ptr<Queue>;
  using QueueSelector = std::function<int(void)>;

  template <typename... Args>
  static QueuePtr CreateQueue(Args&&... args) {
    auto raw_ptr = new Queue(std::forward<Args>(args)...);
    return QueuePtr(raw_ptr);
  }

  Multiplexer() : sem_items_{0} {}

  int RegisterQueue(QueuePtr&& queue) {
    const int id_to_return = queues_.size();
    queues_.push_back(std::move(queue));
    return id_to_return;
  }

  void Push(const int idx, T&& t) {
    CheckIdx(idx);
    queues_[idx]->Push(std::move(t));
    sem_items_.SemPost();
  }

  T Pop(QueueSelector selector) {
    SemWait();
    int q_id = selector();
    CheckIdx(q_id);  // check, if weird, will die there
    QueuePtr& queue = queues_[q_id];
    CHECK(queue) << "queue must not be null.";
    return queue->Pop();
  }

  T Pop() {
    auto default_selector = [this]() -> int {
      for (int i = 0; i < queues_.size(); i++) {
        if (!queues_[i]->IsEmpty()) {
          return i;
        }
      }
      return -1;
    };
    return Pop(default_selector);
  }

  bool IsEmpty(const int idx) { return queues_[idx]->IsEmpty(); }

  void SemWait() { sem_items_.SemWait(); }

 private:
  void CheckIdx(const int idx) {
    CHECK(idx >= 0 && idx < queues_.size()) << "queues_ array out of bound";
  }
  // total items across the queues
  Semaphore sem_items_;
  std::vector<QueuePtr> queues_;
  QueuePtr null_ptr_;
};
}  // end of namespace cuttlefish
