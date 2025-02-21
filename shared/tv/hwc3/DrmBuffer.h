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
#include <optional>
#include <unordered_map>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmClient;

// A RAII object that will clear a drm framebuffer upon destruction.
class DrmBuffer {
 public:
  ~DrmBuffer();

  DrmBuffer(const DrmBuffer&) = delete;
  DrmBuffer& operator=(const DrmBuffer&) = delete;

  DrmBuffer(DrmBuffer&&) = delete;
  DrmBuffer& operator=(DrmBuffer&&) = delete;

 private:
  friend class DrmClient;
  friend class DrmDisplay;
  DrmBuffer(DrmClient& drmClient);

  DrmClient& mDrmClient;

  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  uint32_t mDrmFormat = 0;
  int32_t mPlaneFds[4] = {0, 0, 0, 0};
  uint32_t mPlaneHandles[4] = {0, 0, 0, 0};
  uint32_t mPlanePitches[4] = {0, 0, 0, 0};
  uint32_t mPlaneOffsets[4] = {0, 0, 0, 0};
  std::optional<uint32_t> mDrmFramebuffer;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl
