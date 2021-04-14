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

#include <deque>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include "common/libs/concurrency/semaphore.h"

namespace cuttlefish {
// move-based concurrent queue
template<typename T>
class ScreenConnectorQueue {

 public:
  static const int kQSize = 2;

  static_assert( is_movable<T>::value,
                 "Items in ScreenConnectorQueue should be std::mov-able");

  ScreenConnectorQueue(Semaphore& sc_sem)
      : q_mutex_(std::make_unique<std::mutex>()), sc_semaphore_(sc_sem) {}
  ScreenConnectorQueue(ScreenConnectorQueue&& cq) = delete;
  ScreenConnectorQueue(const ScreenConnectorQueue& cq) = delete;
  ScreenConnectorQueue& operator=(const ScreenConnectorQueue& cq) = delete;
  ScreenConnectorQueue& operator=(ScreenConnectorQueue&& cq) = delete;

  bool Empty() const {
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
   * PushBack( std::move(src) );
   *
   * Note: this queue is suppoed to be used only by ScreenConnector-
   * related components such as ScreenConnectorSource
   *
   * The traditional assumption was that when webRTC or VNC calls
   * OnFrameAfter, the call should be block until it could return
   * one frame.
   *
   * Thus, the producers of this queue must not produce frames
   * much faster than the consumer, VNC or WebRTC consumes.
   * Therefore, when the small buffer is full -- which means
   * VNC or WebRTC would not call OnFrameAfter --, the producer
   * should stop adding itmes to the queue.
   *
   */
  void PushBack(T&& item) {
    std::unique_lock<std::mutex> lock(*q_mutex_);
    if (Full()) {
      auto is_empty =
          [this](void){ return buffer_.empty(); };
      q_empty_.wait(lock, is_empty);
    }
    buffer_.push_back(std::move(item));
    /* Whether the total number of items in ALL queus is 0 or not
     * is tracked via a semaphore shared by all queues
     *
     * This is NOT intended to block queue from pushing an item
     * This IS intended to awake the screen_connector consumer thread
     * when one or more items are available at least in one queue
     */
    sc_semaphore_.SemPost();
  }
  void PushBack(T& item) = delete;
  void PushBack(const T& item) = delete;

  /*
   * PopFront must be preceded by sc_semaphore_.SemWaitItem()
   *
   */
  T PopFront() {
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
    return kQSize == buffer_.size();
  }
  std::deque<T> buffer_;
  std::unique_ptr<std::mutex> q_mutex_;
  std::condition_variable q_empty_;
  Semaphore& sc_semaphore_;
};

} // namespace cuttlefish
