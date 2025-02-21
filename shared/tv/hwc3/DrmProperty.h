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
#include <limits>
#include <string>
#include <unordered_map>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmProperty {
 public:
  DrmProperty() {}
  DrmProperty(uint32_t id, uint64_t value, std::string name)
      : mId(id), mValue(value), mName(name) {}

  ~DrmProperty() {}

  uint32_t getId() const { return mId; }

  uint64_t getValue() const { return mValue; }

  const std::string& getName() const { return mName; }

 private:
  uint32_t mId = std::numeric_limits<uint32_t>::max();
  uint64_t mValue = std::numeric_limits<uint64_t>::max();
  std::string mName;
};

template <typename T>
using DrmPropertyMember = DrmProperty T::*;

template <typename T>
using DrmPropertyMemberMap =
    std::unordered_map<std::string, DrmPropertyMember<T>>;

// Helper to many DrmProperty members for DrmCrtc, DrmConnector, and DrmPlane.
template <typename T>
bool LoadDrmProperties(::android::base::borrowed_fd drmFd, uint32_t objectId,
                       uint32_t objectType,
                       const DrmPropertyMemberMap<T>& objectPropertyMap,
                       T* object) {
  auto drmProperties =
      drmModeObjectGetProperties(drmFd.get(), objectId, objectType);
  if (!drmProperties) {
    ALOGE("%s: Failed to get properties: %s", __FUNCTION__, strerror(errno));
    return false;
  }

  for (uint32_t i = 0; i < drmProperties->count_props; ++i) {
    const auto propertyId = drmProperties->props[i];

    auto drmProperty = drmModeGetProperty(drmFd.get(), propertyId);
    if (!drmProperty) {
      ALOGE("%s: Failed to get property: %s", __FUNCTION__, strerror(errno));
      continue;
    }

    const auto propertyName = drmProperty->name;
    const auto propertyValue = drmProperties->prop_values[i];

    auto it = objectPropertyMap.find(propertyName);
    if (it != objectPropertyMap.end()) {
      DEBUG_LOG("%s: Loaded property:%" PRIu32 " (%s) val:%" PRIu64,
                __FUNCTION__, propertyId, propertyName, propertyValue);

      auto& objectPointerToMember = it->second;
      object->*objectPointerToMember =
          DrmProperty(propertyId, propertyValue, propertyName);
    }

    drmModeFreeProperty(drmProperty);
  }

  drmModeFreeObjectProperties(drmProperties);

  return true;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
