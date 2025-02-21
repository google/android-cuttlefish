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

#include <android-base/unique_fd.h>
#include <cutils/native_handle.h>

#include <memory>
#include <tuple>
#include <vector>

#include "Common.h"
#include "DrmAtomicRequest.h"
#include "DrmBuffer.h"
#include "DrmConnector.h"
#include "DrmCrtc.h"
#include "DrmDisplay.h"
#include "DrmEventListener.h"
#include "DrmMode.h"
#include "DrmPlane.h"
#include "DrmProperty.h"
#include "LruCache.h"
#include "aemu/base/synchronization/AndroidLock.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmClient {
 public:
  DrmClient() = default;
  ~DrmClient();

  DrmClient(const DrmClient&) = delete;
  DrmClient& operator=(const DrmClient&) = delete;

  DrmClient(DrmClient&&) = delete;
  DrmClient& operator=(DrmClient&&) = delete;

  ::android::base::unique_fd OpenVirtioGpuDrmFd();

  HWC3::Error init();

  struct DisplayConfig {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t dpiX;
    uint32_t dpiY;
    uint32_t refreshRateHz;
  };

  HWC3::Error getDisplayConfigs(std::vector<DisplayConfig>* configs) const;

  using HotplugCallback = std::function<void(bool /*connected*/,   //
                                             uint32_t /*id*/,      //
                                             uint32_t /*width*/,   //
                                             uint32_t /*height*/,  //
                                             uint32_t /*dpiX*/,    //
                                             uint32_t /*dpiY*/,    //
                                             uint32_t /*refreshRate*/)>;

  HWC3::Error registerOnHotplugCallback(const HotplugCallback& cb);
  HWC3::Error unregisterOnHotplugCallback();

  uint32_t refreshRate() const { return mDisplays[0]->getRefreshRateUint(); }

  std::tuple<HWC3::Error, std::shared_ptr<DrmBuffer>> create(
      const native_handle_t* handle);

  std::tuple<HWC3::Error, ::android::base::unique_fd> flushToDisplay(
      uint32_t displayId, const std::shared_ptr<DrmBuffer>& buffer,
      ::android::base::borrowed_fd inWaitSyncFd);

  std::optional<std::vector<uint8_t>> getEdid(uint32_t displayId);

 private:
  using DrmPrimeBufferHandle = uint32_t;

  // Grant visibility to destroyDrmFramebuffer to DrmBuffer.
  friend class DrmBuffer;
  HWC3::Error destroyDrmFramebuffer(DrmBuffer* buffer);

  // Grant visibility for handleHotplug to DrmEventListener.
  bool handleHotplug();

  bool loadDrmDisplays();

  // Drm device.
  ::android::base::unique_fd mFd;

  mutable ::gfxstream::guest::ReadWriteLock mDisplaysMutex;
  std::vector<std::unique_ptr<DrmDisplay>> mDisplays;

  std::optional<HotplugCallback> mHotplugCallback;

  std::unique_ptr<DrmEventListener> mDrmEventListener;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl
