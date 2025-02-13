/*
 * Copyright 2022 The Android Open Source Project
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

#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <thread>
#include <unordered_map>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmEventListener {
 public:
  static std::unique_ptr<DrmEventListener> create(
      ::android::base::borrowed_fd drmFd, std::function<void()> callback);

  ~DrmEventListener() {}

 private:
  DrmEventListener(std::function<void()> callback)
      : mOnEventCallback(std::move(callback)) {}

  bool init(::android::base::borrowed_fd drmFd);

  void threadLoop();

  std::thread mThread;
  std::function<void()> mOnEventCallback;
  ::android::base::unique_fd mEventFd;
  fd_set mMonitoredFds;
  int mMaxMonitoredFd = 0;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl
