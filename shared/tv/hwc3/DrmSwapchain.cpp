/*
 * Copyright 2023 The Android Open Source Project
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

#include "DrmSwapchain.h"

#include <log/log.h>
#include <sync/sync.h>
#include <ui/GraphicBufferAllocator.h>

namespace aidl::android::hardware::graphics::composer3::impl {

DrmSwapchain::Image::Image(const native_handle_t* buffer,
                           std::shared_ptr<DrmBuffer> drmBuffer)
    : mBuffer(buffer), mDrmBuffer(drmBuffer) {}

DrmSwapchain::Image::Image(Image&& other)
    : mBuffer(std::move(other.mBuffer)),
      mDrmBuffer(std::move(other.mDrmBuffer)),
      mLastUseFenceFd(std::move(other.mLastUseFenceFd)) {
  other.mBuffer = nullptr;
}

DrmSwapchain::Image::~Image() {
  if (mBuffer) {
    ::android::GraphicBufferAllocator::get().free(mBuffer);
  }
}

int DrmSwapchain::Image::wait() {
  if (!mLastUseFenceFd.ok()) {
    return 0;
  }
  int err = sync_wait(mLastUseFenceFd.get(), 3000);
  mLastUseFenceFd = ::android::base::unique_fd();
  if (err < 0 && errno == ETIME) {
    ALOGE("%s waited on fence %d for 3000 ms", __FUNCTION__,
          mLastUseFenceFd.get());
  }
  if (err < 0) {
    return err;
  }
  return 0;
}

void DrmSwapchain::Image::markAsInUse(
    ::android::base::unique_fd useCompleteFenceFd) {
  mLastUseFenceFd = std::move(useCompleteFenceFd);
}

const native_handle_t* DrmSwapchain::Image::getBuffer() { return mBuffer; }

const std::shared_ptr<DrmBuffer> DrmSwapchain::Image::getDrmBuffer() {
  return mDrmBuffer;
}

std::unique_ptr<DrmSwapchain> DrmSwapchain::create(uint32_t width,
                                                   uint32_t height,
                                                   uint32_t usage,
                                                   DrmClient* client,
                                                   uint32_t numImages) {
  DEBUG_LOG("%s: creating swapchain w:%" PRIu32 " h:%" PRIu32 " usage:%" PRIu32
            " count:%" PRIu32,
            __FUNCTION__, width, height, usage, numImages);
  std::vector<Image> images;
  for (uint32_t i = 0; i < numImages; i++) {
    const uint32_t layerCount = 1;
    buffer_handle_t handle;
    uint32_t stride;
    if (::android::GraphicBufferAllocator::get().allocate(
            width, height, ::android::PIXEL_FORMAT_RGBA_8888, layerCount, usage,
            &handle, &stride, "RanchuHwc") != ::android::OK) {
      ALOGE("%s: Failed to allocate drm ahb", __FUNCTION__);
      return nullptr;
    }
    auto ahb = static_cast<const native_handle_t*>(handle);

    HWC3::Error drmBufferCreateError;
    std::shared_ptr<DrmBuffer> drmBuffer;
    if (client) {
      std::tie(drmBufferCreateError, drmBuffer) = client->create(ahb);
      if (drmBufferCreateError != HWC3::Error::None) {
        ALOGE("%s: failed to create target drm ahb", __FUNCTION__);
        return nullptr;
      }
    }

    images.emplace_back(Image(ahb, std::move(drmBuffer)));
  }
  return std::unique_ptr<DrmSwapchain>(new DrmSwapchain(std::move(images)));
}

DrmSwapchain::DrmSwapchain(std::vector<Image> images)
    : mImages(std::move(images)) {}

DrmSwapchain::Image* DrmSwapchain::getNextImage() {
  auto index = (mLastUsedIndex + 1) % mImages.size();
  mLastUsedIndex = index;
  return &mImages[index];
}

}  // namespace aidl::android::hardware::graphics::composer3::impl