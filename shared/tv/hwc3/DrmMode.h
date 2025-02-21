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

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmMode {
 public:
  static std::unique_ptr<DrmMode> create(::android::base::borrowed_fd drmFd,
                                         const drmModeModeInfo& info);

  ~DrmMode();

  const uint32_t clock;
  const uint16_t hdisplay;
  const uint16_t hsync_start;
  const uint16_t hsync_end;
  const uint16_t htotal;
  const uint16_t hskew;
  const uint16_t vdisplay;
  const uint16_t vsync_start;
  const uint16_t vsync_end;
  const uint16_t vtotal;
  const uint16_t vscan;
  const uint32_t vrefresh;
  const uint32_t flags;
  const uint32_t type;
  const std::string name;

  uint32_t getBlobId() const { return mBlobId; }

 private:
  DrmMode(const drmModeModeInfo& info, uint32_t blobId)
      : clock(info.clock),
        hdisplay(info.hdisplay),
        hsync_start(info.hsync_start),
        hsync_end(info.hsync_end),
        htotal(info.htotal),
        hskew(info.hskew),
        vdisplay(info.vdisplay),
        vsync_start(info.vsync_start),
        vsync_end(info.vsync_end),
        vtotal(info.vtotal),
        vscan(info.vscan),
        vrefresh(info.vrefresh),
        flags(info.flags),
        type(info.type),
        name(info.name),
        mBlobId(blobId) {}

  const uint32_t mBlobId;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl
