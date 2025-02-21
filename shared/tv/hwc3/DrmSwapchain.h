/*
 * Copyright (C) 2023 The Android Open Source Project
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
#ifndef ANDROID_HWC_DRMSWAPCHAIN_H
#define ANDROID_HWC_DRMSWAPCHAIN_H

#include <android-base/unique_fd.h>

#include "Common.h"
#include "DrmClient.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmSwapchain {
 public:
  class Image {
   public:
    Image() = delete;
    ~Image();
    int wait();
    void markAsInUse(::android::base::unique_fd useCompleteFenceFd);
    const native_handle_t* getBuffer();
    const std::shared_ptr<DrmBuffer> getDrmBuffer();
    Image(Image&& other);

   private:
    Image(const native_handle_t*, std::shared_ptr<DrmBuffer>);
    const native_handle_t* mBuffer = nullptr;
    std::shared_ptr<DrmBuffer> mDrmBuffer;
    ::android::base::unique_fd mLastUseFenceFd;

    friend class DrmSwapchain;
  };

  static std::unique_ptr<DrmSwapchain> create(uint32_t width, uint32_t height,
                                              uint32_t usage, DrmClient* client,
                                              uint32_t numImages = 3);
  Image* getNextImage();

 private:
  DrmSwapchain(std::vector<Image> images);
  std::vector<Image> mImages;
  std::size_t mLastUsedIndex = 0;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif