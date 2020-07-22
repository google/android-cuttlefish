//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/modem_simulator/thread_looper.h"

#include <android-base/logging.h>

namespace cuttlefish {

ThreadLooper::ThreadLooper()
  :   stopped_(false), next_serial_(1) {
  looper_thread_ = std::thread([this]() { ThreadLoop(); });
}

ThreadLooper::~ThreadLooper() { Stop(); }

bool ThreadLooper::Event::operator<=(const Event &other) const {
  return when <= other.when;
}

ThreadLooper::Serial ThreadLooper::Post(Callback cb) {
  CHECK(cb != nullptr);

  auto serial = next_serial_++;
  // If it's the time to process event with delay exactly when posting
  // a event without delay. Looper would process the event without delay firstly
  // if when set to be std::nullptr. so set when_ to be now.
  Insert({ std::chrono::steady_clock::now(), cb, serial });

  return serial;
}

ThreadLooper::Serial ThreadLooper::PostWithDelay(
        std::chrono::steady_clock::duration delay, Callback cb) {
  CHECK(cb != nullptr);

  auto serial = next_serial_++;
  Insert({ std::chrono::steady_clock::now() + delay, cb, serial });

  return serial;
}

bool ThreadLooper::CancelSerial(Serial serial) {
  std::lock_guard<std::mutex> autolock(lock_);

  bool found = false;
  for (auto iter = queue_.begin(); iter != queue_.end(); ++iter) {
    if (iter->serial == serial) {
      queue_.erase(iter);
      cond_.notify_all();

      found = true;
      break;
    }
  }

  return found;
}

void ThreadLooper::Insert(const Event &event) {
  std::lock_guard<std::mutex> autolock(lock_);

  auto iter = queue_.begin();
  while (iter != queue_.end() && *iter <= event) {
    ++iter;
  }

  queue_.insert(iter, event);
  cond_.notify_all();
}

void ThreadLooper::ThreadLoop() {
  for(;;) {
    Callback cb;
    {
      std::unique_lock<std::mutex> lock(lock_);

      if (stopped_) {
        break;
      }

      if (queue_.empty()) {
        cond_.wait(lock);
        continue;
      }

      auto time_to_wait = queue_.front().when - std::chrono::steady_clock::now();
      if (time_to_wait.count() > 0) {
        // wait with timeout
        auto durationMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(time_to_wait);
        cond_.wait_for(lock, durationMs);
        continue;
      }
      cb = queue_.front().cb; // callback at front of queue
      queue_.pop_front();
    }
    cb();
  }
}

void ThreadLooper::Stop() {
  if (stopped_) {
    return;
  }
  CHECK(looper_thread_.get_id() != std::this_thread::get_id())
      << "Destructor called from looper thread";
  {
    std::lock_guard<std::mutex> autolock(lock_);
    stopped_ = true;
  }
  cond_.notify_all();
  if (looper_thread_.joinable()) {
    looper_thread_.join();
  }
}

}  // namespace cuttlefish
