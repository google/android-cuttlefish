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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

#include <fruit/fruit.h>

#include "common/libs/confui/confui.h"
#include "host/libs/confui/host_utils.h"

namespace cuttlefish {
/**
 * mechanism to orchestrate concurrent executions of threads
 * that work for screen connector
 *
 * Within WebRTC service, it tells when it is now in the Android Mode or
 * Confirmation UI mode
 */
class HostModeCtrl {
 public:
  enum class ModeType : std::uint8_t { kAndroidMode = 55, kConfUI_Mode = 77 };
  INJECT(HostModeCtrl()) : atomic_mode_(ModeType::kAndroidMode) {}
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
    ConfUiLog(DEBUG) << cuttlefish::confui::thread::GetName()
                     << "checking atomic Android mode";
    if (atomic_mode_ == ModeType::kAndroidMode) {
      ConfUiLog(DEBUG) << cuttlefish::confui::thread::GetName()
                       << "returns as it is already Android mode";
      return;
    }
    auto check = [this]() -> bool {
      return atomic_mode_ == ModeType::kAndroidMode;
    };
    std::unique_lock<std::mutex> lock(mode_mtx_);
    and_mode_cv_.wait(lock, check);
    ConfUiLog(DEBUG) << cuttlefish::confui::thread::GetName()
                     << "awakes from cond var waiting for Android mode";
  }

  void SetMode(const ModeType mode) {
    ConfUiLog(DEBUG) << cuttlefish::confui::thread::GetName()
                     << " tries to acquire the lock in SetMode";
    std::lock_guard<std::mutex> lock(mode_mtx_);
    ConfUiLog(DEBUG) << cuttlefish::confui::thread::GetName()
                     << " acquired the lock in SetMode";
    atomic_mode_ = mode;
    if (atomic_mode_ == ModeType::kAndroidMode) {
      ConfUiLog(DEBUG) << cuttlefish::confui::thread::GetName()
                       << " signals kAndroidMode in SetMode";
      and_mode_cv_.notify_all();
      return;
    }
    ConfUiLog(DEBUG) << cuttlefish::confui::thread::GetName()
                     << "signals kConfUI_Mode in SetMode";
    confui_mode_cv_.notify_all();
  }

  auto GetMode() {
    ModeType ret_val = atomic_mode_;
    return ret_val;
  }

  auto IsConfirmatioUiMode() {
    return (atomic_mode_ == ModeType::kConfUI_Mode);
  }

  auto IsAndroidMode() { return (atomic_mode_ == ModeType::kAndroidMode); }

  static HostModeCtrl& Get() {
    static HostModeCtrl host_mode_controller;
    return host_mode_controller;
  }

 private:
  std::mutex mode_mtx_;
  std::condition_variable and_mode_cv_;
  std::condition_variable confui_mode_cv_;
  std::atomic<ModeType> atomic_mode_;
};
}  // end of namespace cuttlefish
