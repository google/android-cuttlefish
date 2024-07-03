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
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "common/libs/concurrency/semaphore.h"

namespace cuttlefish {
// move-based concurrent queue
template <typename T>
class ScreenConnectorQueue {
 public:
  static_assert(is_movable<T>::value,
                "Items in ScreenConnectorQueue should be std::mov-able");

  ScreenConnectorQueue(const int q_max_size = 2)
      : q_mutex_(std::make_unique<std::mutex>()), q_max_size_{q_max_size} {}
  ScreenConnectorQueue(ScreenConnectorQueue&& cq) = delete;
  ScreenConnectorQueue(const ScreenConnectorQueue& cq) = delete;
  ScreenConnectorQueue& operator=(const ScreenConnectorQueue& cq) = delete;
  ScreenConnectorQueue& operator=(ScreenConnectorQueue&& cq) = delete;

  bool IsEmpty() const {
    const std::lock_guard<std::mutex> lock(*q_mutex_);
    return buffer_.empty();
  }

  auto Size() const {
    const std::lock_guard<std::mutex> lock(*q_mutex_);
    return buffer_.size();
  }

  void WaitEmpty() {
    auto is_empty = [this](void) { return buffer_.empty(); };
    std::unique_lock<std::mutex> lock(*q_mutex_);
    q_empty_.wait(lock, is_empty);
  }

  /*
   * Push( std::move(src) );
   *
   * Note: this queue is supposed to be used only by ScreenConnector-
   * related components such as ScreenConnectorSource
   *
   * The traditional assumption was that when webRTC calls
   * OnFrameAfter, the call should be block until it could return
   * one frame.
   *
   * Thus, the producers of this queue must not produce frames
   * much faster than the consumer, WebRTC consumes.
   * Therefore, when the small buffer is full -- which means
   * WebRTC would not call OnNextFrame --, the producer
   * should stop adding items to the queue.
   *
   */
  void Push(T&& item) {
    std::unique_lock<std::mutex> lock(*q_mutex_);
    if (Full()) {
      auto is_empty = [this](void) { return buffer_.empty(); };
      q_empty_.wait(lock, is_empty);
    }
    buffer_.push_back(std::move(item));
  }
  void Push(T& item) = delete;
  void Push(const T& item) = delete;

  T Pop() {
    const std::lock_guard<std::mutex> lock(*q_mutex_);
    auto item = std::move(buffer_.front());
    buffer_.pop_front();
    if (buffer_.empty()) {
      q_empty_.notify_all();
    }
    return item;
  }

 private:
  bool Full() const {
    // call this in a critical section
    // after acquiring q_mutex_
    return q_max_size_ == buffer_.size();
  }
  std::deque<T> buffer_;
  std::unique_ptr<std::mutex> q_mutex_;
  std::condition_variable q_empty_;
  const int q_max_size_;
};

}  // namespace cuttlefish
