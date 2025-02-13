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

#include "DrmAtomicRequest.h"

namespace aidl::android::hardware::graphics::composer3::impl {

std::unique_ptr<DrmAtomicRequest> DrmAtomicRequest::create() {
  drmModeAtomicReqPtr request = drmModeAtomicAlloc();
  if (!request) {
    return nullptr;
  }

  return std::unique_ptr<DrmAtomicRequest>(new DrmAtomicRequest(request));
}

DrmAtomicRequest::~DrmAtomicRequest() {
  if (mRequest) {
    drmModeAtomicFree(mRequest);
  }
}

bool DrmAtomicRequest::Set(uint32_t objectId, const DrmProperty& prop,
                           uint64_t value) {
  int ret = drmModeAtomicAddProperty(mRequest, objectId, prop.getId(), value);
  if (ret < 0) {
    ALOGE("%s: failed to set atomic request property %s to %" PRIu64 ": %s",
          __FUNCTION__, prop.getName().c_str(), value, strerror(errno));
    return false;
  }
  return true;
}

bool DrmAtomicRequest::Commit(::android::base::borrowed_fd drmFd) {
  constexpr const uint32_t kCommitFlags = DRM_MODE_ATOMIC_ALLOW_MODESET;

  int ret = drmModeAtomicCommit(drmFd.get(), mRequest, kCommitFlags, 0);
  if (ret) {
    ALOGE("%s:%d: atomic commit failed: %s\n", __FUNCTION__, __LINE__,
          strerror(errno));
    return false;
  }

  return true;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
