#pragma once

/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <condition_variable>
#include <deque>
#include <iterator>
#include <mutex>
#include <type_traits>
#include <utility>

namespace cuttlefish {
// Simple queue with Push and Pop capabilities.
// If the max_elements argument is passed to the constructor, and Push is called
// when the queue holds max_elements items, the max_elements_handler is called
// with a pointer to the internal QueueImpl. The call is made while holding
// the guarding mutex; operations on the QueueImpl will not interleave with
// other threads calling Push() or Pop().
// The QueueImpl type will be a SequenceContainer.
template <typename T>
class ThreadSafeQueue {
 public:
  using QueueImpl = std::deque<T>;
  using QueueFullHandler = std::function<void(QueueImpl*)>;

  ThreadSafeQueue() = default;
  explicit ThreadSafeQueue(std::size_t max_elements,
                           QueueFullHandler max_elements_handler)
      : max_elements_{max_elements},
        max_elements_handler_{std::move(max_elements_handler)} {}

  T Pop() {
    std::unique_lock<std::mutex> guard(m_);
    while (items_.empty()) {
      new_item_.wait(guard);
    }
    auto t = std::move(items_.front());
    items_.pop_front();
    return t;
  }

  QueueImpl PopAll() {
    std::unique_lock<std::mutex> guard(m_);
    while (items_.empty()) {
      new_item_.wait(guard);
    }
    return std::move(items_);
  }

  template <typename U>
  bool Push(U&& u) {
    static_assert(std::is_assignable_v<T, decltype(u)>);
    std::lock_guard<std::mutex> guard(m_);
    const bool has_room = DropItemsIfAtCapacity();
    if (!has_room) {
      return false;
    }
    items_.push_back(std::forward<U>(u));
    new_item_.notify_one();
    return true;
  }

  bool IsEmpty() {
    std::lock_guard<std::mutex> guard(m_);
    return items_.empty();
  }

  bool IsFull() {
    std::lock_guard<std::mutex> guard(m_);
    return items_.size() == max_elements_;
  }

 private:
  // return whether there's room to push
  bool DropItemsIfAtCapacity() {
    if (max_elements_ && max_elements_ == items_.size()) {
      max_elements_handler_(&items_);
    }
    if (max_elements_ && max_elements_ == items_.size()) {
      // handler intends to ignore the newly coming element or
      // did not empty the room for whatever reason
      return false;
    }
    return true;
  }

  std::mutex m_;
  std::size_t max_elements_{};
  QueueFullHandler max_elements_handler_{};
  std::condition_variable new_item_;
  QueueImpl items_;
};
}  // namespace cuttlefish
