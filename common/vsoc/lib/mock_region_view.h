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
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "common/vsoc/lib/region_signaling_interface.h"

namespace vsoc {
namespace test {

/**
 * MockRegionView mocks a region in the shared memory window by calloc and
 * futex. It supports only one-sided signal as in it doesn't do anything on
 * sending or waiting interrupt. This is to test if a particular layout
 * behaves correctly when there are multiple threads accessing it.
 */
template <typename Layout>
class MockRegionView : public vsoc::RegionSignalingInterface {
 public:
  explicit MockRegionView(){};
  virtual ~MockRegionView() {
    if (region_base_) {
      free(region_base_);
      region_base_ = nullptr;
    }
  };

  bool Open() {
    region_base_ = calloc(sizeof(Layout), 1);
    return !region_base_;
  };

  virtual void SendSignal(vsoc::layout::Sides /* sides_to_signal */,
                          std::atomic<uint32_t>* uaddr) {
    syscall(SYS_futex, reinterpret_cast<int32_t*>(uaddr), FUTEX_WAKE, -1,
            nullptr, nullptr, 0);
  }

  virtual int WaitForSignal(std::atomic<uint32_t>* uaddr,
                             uint32_t expected_value) {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (tid_to_addr_.find(std::this_thread::get_id()) != tid_to_addr_.end()) {
        // Same thread tries to wait
        return 0;
      }
      tid_to_addr_.emplace(std::this_thread::get_id(), uaddr);
      map_changed_.notify_one();
    }

    syscall(SYS_futex, uaddr, FUTEX_WAIT, expected_value, nullptr, nullptr, 0);

    {
      std::lock_guard<std::mutex> guard(mutex_);
      tid_to_addr_.erase(std::this_thread::get_id());
    }
    return 0;
  }

  // Mock methods from TypedRegionView
  Layout* data() { return reinterpret_cast<Layout*>(region_base_); };

  // Check wait status on a specificy thread
  bool IsBlocking(std::thread::id tid) {
    while (1) {
      std::unique_lock<std::mutex> lock(mutex_);
      if (tid_to_addr_.find(tid) != tid_to_addr_.end()) {
        return true;
      }
      // Allow some time as tid map might not be updated yet
      while (tid_to_addr_.find(tid) == tid_to_addr_.end()) {
        if (map_changed_.wait_for(lock,
                                  std::chrono::seconds(kWaitTimeoutInSec)) ==
            std::cv_status::timeout) {
          return false;
        }
      }
      return true;
    }
  }

 private:
  // Timeout to avoid a race on checking if a thread is blocked
  static constexpr int kWaitTimeoutInSec = 5;

  void* region_base_{};
  std::mutex mutex_;
  std::condition_variable map_changed_;
  std::unordered_map<std::thread::id, std::atomic<uint32_t>*> tid_to_addr_;
};

template <typename Layout>
constexpr int MockRegionView<Layout>::kWaitTimeoutInSec;

}  // namespace test
}  // namespace vsoc
