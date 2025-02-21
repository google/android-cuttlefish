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

#include "DrmMode.h"

namespace aidl::android::hardware::graphics::composer3::impl {

std::unique_ptr<DrmMode> DrmMode::create(::android::base::borrowed_fd drmFd,
                                         const drmModeModeInfo& info) {
  uint32_t blobId = 0;

  int ret =
      drmModeCreatePropertyBlob(drmFd.get(), &info, sizeof(info), &blobId);
  if (ret != 0) {
    ALOGE("%s: Failed to create mode blob: %s.", __FUNCTION__, strerror(errno));
    return nullptr;
  }

  return std::unique_ptr<DrmMode>(new DrmMode(info, blobId));
}

DrmMode::~DrmMode() {
  // TODO: don't leak the blob.
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
