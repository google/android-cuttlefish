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
#include <memory>
#include <string>
#include <unordered_map>

#include "Common.h"
#include "DrmBuffer.h"
#include "DrmConnector.h"
#include "DrmCrtc.h"
#include "DrmPlane.h"

namespace aidl::android::hardware::graphics::composer3::impl {

enum class DrmHotplugChange {
  kNoChange,
  kConnected,
  kDisconnected,
};

class DrmDisplay {
 public:
  static std::unique_ptr<DrmDisplay> create(
      uint32_t id, std::unique_ptr<DrmConnector> connector,
      std::unique_ptr<DrmCrtc> crtc, std::unique_ptr<DrmPlane> plane,
      ::android::base::borrowed_fd drmFd);

  uint32_t getId() const { return mId; }

  uint32_t getWidth() const { return mConnector->getWidth(); }
  uint32_t getHeight() const { return mConnector->getHeight(); }

  uint32_t getDpiX() const { return mConnector->getDpiX(); }
  uint32_t getDpiY() const { return mConnector->getDpiY(); }

  uint32_t getRefreshRateUint() const {
    return mConnector->getRefreshRateUint();
  }

  bool isConnected() const { return mConnector->isConnected(); }

  std::optional<std::vector<uint8_t>> getEdid() const {
    return mConnector->getEdid();
  }

  std::tuple<HWC3::Error, ::android::base::unique_fd> flush(
      ::android::base::borrowed_fd drmFd,
      ::android::base::borrowed_fd inWaitSyncFd,
      const std::shared_ptr<DrmBuffer>& buffer);

  DrmHotplugChange checkAndHandleHotplug(::android::base::borrowed_fd drmFd);

 private:
  DrmDisplay(uint32_t id, std::unique_ptr<DrmConnector> connector,
             std::unique_ptr<DrmCrtc> crtc, std::unique_ptr<DrmPlane> plane)
      : mId(id),
        mConnector(std::move(connector)),
        mCrtc(std::move(crtc)),
        mPlane(std::move(plane)) {}

  bool onConnect(::android::base::borrowed_fd drmFd);

  bool onDisconnect(::android::base::borrowed_fd drmFd);

  const uint32_t mId;
  std::unique_ptr<DrmConnector> mConnector;
  std::unique_ptr<DrmCrtc> mCrtc;
  std::unique_ptr<DrmPlane> mPlane;

  // The last presented buffer / DRM framebuffer is cached until
  // the next present to avoid toggling the display on and off.
  std::shared_ptr<DrmBuffer> mPreviousBuffer;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl
