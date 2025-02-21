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

#include "DrmPlane.h"

namespace aidl::android::hardware::graphics::composer3::impl {

std::unique_ptr<DrmPlane> DrmPlane::create(::android::base::borrowed_fd drmFd,
                                           uint32_t planeId) {
  std::unique_ptr<DrmPlane> plane(new DrmPlane(planeId));

  DEBUG_LOG("%s: Loading properties for DRM plane:%" PRIu32, __FUNCTION__,
            planeId);
  if (!LoadDrmProperties(drmFd, planeId, DRM_MODE_OBJECT_PLANE,
                         GetPropertiesMap(), plane.get())) {
    ALOGE("%s: Failed to load plane properties.", __FUNCTION__);
    return nullptr;
  }

  drmModePlanePtr drmPlane = drmModeGetPlane(drmFd.get(), planeId);
  plane->mPossibleCrtcsMask = drmPlane->possible_crtcs;
  drmModeFreePlane(drmPlane);

  return plane;
}

bool DrmPlane::isPrimary() const {
  return mType.getValue() == DRM_PLANE_TYPE_PRIMARY;
}

bool DrmPlane::isOverlay() const {
  return mType.getValue() == DRM_PLANE_TYPE_OVERLAY;
}

bool DrmPlane::isCompatibleWith(const DrmCrtc& crtc) {
  return ((0x1 << crtc.mIndexInResourcesArray) & mPossibleCrtcsMask);
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
