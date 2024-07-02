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

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "common/libs/concurrency/semaphore.h"

namespace cuttlefish {
/**
 * mechanism to orchestrate concurrent executions of threads
 * that work for screen connector
 *
 * One thing is when any of wayland/socket-based connector or
 * confirmation UI has a frame, it should wake up the consumer
 * The two queues are separate, so the conditional variables,
 * etc, can't be in the queue
 */
class ScreenConnectorCtrl {
 public:
  enum class ModeType { kAndroidMode, kConfUI_Mode };

  ScreenConnectorCtrl() : atomic_mode_(ModeType::kAndroidMode) {}

  /**
   * The thread that enqueues Android frames will call this to wait until
   * the mode is kAndroidMode
   *
   * Logically, using atomic_mode_ alone is not sufficient. Using mutex alone
   * is logically complete but slow.
   *
   * Note that most of the time, the mode is kAndroidMode. Also, note that
   * this method is called at every single frame.
   *
   * As an optimization, we check atomic_mode_ first. If failed, we wait for
   * kAndroidMode with mutex-based lock
   *
   * The actual synchronization is not at the and_mode_cv_.wait line but at
   * this line:
   *     if (atomic_mode_ == ModeType::kAndroidMode) {
   *
   * This trick reduces the flag checking delays by 70+% on a Gentoo based
   * amd64 desktop, with Linux 5.10
   */
  void WaitAndroidMode() {
    if (atomic_mode_ == ModeType::kAndroidMode) {
      return;
    }
    auto check = [this]() -> bool {
      return atomic_mode_ == ModeType::kAndroidMode;
    };
    std::unique_lock<std::mutex> lock(mode_mtx_);
    and_mode_cv_.wait(lock, check);
  }

  void SetMode(const ModeType mode) {
    std::lock_guard<std::mutex> lock(mode_mtx_);
    atomic_mode_ = mode;
    if (atomic_mode_ == ModeType::kAndroidMode) {
      and_mode_cv_.notify_all();
    }
  }

  auto GetMode() {
    std::lock_guard<std::mutex> lock(mode_mtx_);
    ModeType ret_val = atomic_mode_;
    return ret_val;
  }

  void SemWait() { sem_.SemWait(); }

  // Only called by the producers
  void SemPost() { sem_.SemPost(); }

 private:
  std::mutex mode_mtx_;
  std::condition_variable and_mode_cv_;
  std::atomic<ModeType> atomic_mode_;

  // track the total number of items in all queues
  Semaphore sem_;
};

}  // namespace cuttlefish
