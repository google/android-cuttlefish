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

#include "DrmConnector.h"

#include <span>

#include "EdidInfo.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

static constexpr const float kMillimetersPerInch = 25.4f;

}  // namespace

std::unique_ptr<DrmConnector> DrmConnector::create(
    ::android::base::borrowed_fd drmFd, uint32_t connectorId) {
  std::unique_ptr<DrmConnector> connector(new DrmConnector(connectorId));

  if (!connector->update(drmFd)) {
    return nullptr;
  }

  return connector;
}

bool DrmConnector::update(::android::base::borrowed_fd drmFd) {
  DEBUG_LOG("%s: Loading properties for connector:%" PRIu32, __FUNCTION__, mId);

  if (!LoadDrmProperties(drmFd, mId, DRM_MODE_OBJECT_CONNECTOR,
                         GetPropertiesMap(), this)) {
    ALOGE("%s: Failed to load connector properties.", __FUNCTION__);
    return false;
  }

  drmModeConnector* drmConnector = drmModeGetConnector(drmFd.get(), mId);
  if (!drmConnector) {
    ALOGE("%s: Failed to load connector.", __FUNCTION__);
    return false;
  }

  mStatus = drmConnector->connection;

  mModes.clear();
  for (uint32_t i = 0; i < drmConnector->count_modes; i++) {
    auto mode = DrmMode::create(drmFd, drmConnector->modes[i]);
    if (!mode) {
      ALOGE("%s: Failed to create mode for connector.", __FUNCTION__);
      return false;
    }

    mModes.push_back(std::move(mode));
  }

  if (mStatus == DRM_MODE_CONNECTED) {
    std::optional<EdidInfo> maybeEdidInfo = loadEdid(drmFd);
    if (maybeEdidInfo) {
      const EdidInfo& edidInfo = maybeEdidInfo.value();
      mWidthMillimeters = edidInfo.mWidthMillimeters;
      mHeightMillimeters = edidInfo.mHeightMillimeters;
    } else {
      ALOGW(
          "%s: Use fallback size from drmModeConnector. This can result "
          "inaccurate DPIs.",
          __FUNCTION__);
      mWidthMillimeters = drmConnector->mmWidth;
      mHeightMillimeters = drmConnector->mmHeight;
    }
  }

  DEBUG_LOG("%s: connector:%" PRIu32 " widthMillimeters:%" PRIu32
            " heightMillimeters:%" PRIu32,
            __FUNCTION__, mId, (mWidthMillimeters ? *mWidthMillimeters : 0),
            (mHeightMillimeters ? *mHeightMillimeters : 0));

  drmModeFreeConnector(drmConnector);
  return true;
}

std::optional<EdidInfo> DrmConnector::loadEdid(
    ::android::base::borrowed_fd drmFd) {
  DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

  const uint64_t edidBlobId = mEdidProp.getValue();
  if (edidBlobId == -1) {
    ALOGW("%s: display:%" PRIu32 " does not have EDID.", __FUNCTION__, mId);
    return std::nullopt;
  }

  auto blob =
      drmModeGetPropertyBlob(drmFd.get(), static_cast<uint32_t>(edidBlobId));
  if (!blob) {
    ALOGE("%s: display:%" PRIu32 " failed to read EDID blob (%" PRIu64 "): %s",
          __FUNCTION__, mId, edidBlobId, strerror(errno));
    return std::nullopt;
  }

  const uint8_t* blobStart = static_cast<uint8_t*>(blob->data);
  mEdid = std::vector<uint8_t>(blobStart, blobStart + blob->length);

  drmModeFreePropertyBlob(blob);

  using byte_view = std::span<const uint8_t>;
  byte_view edid(*mEdid);

  return EdidInfo::parse(edid);
}

uint32_t DrmConnector::getWidth() const {
  DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

  if (mModes.empty()) {
    return 0;
  }
  return mModes[0]->hdisplay;
}

uint32_t DrmConnector::getHeight() const {
  DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

  if (mModes.empty()) {
    return 0;
  }
  return mModes[0]->vdisplay;
}

uint32_t DrmConnector::getDpiX() const {
  DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

  if (mModes.empty()) {
    return 0;
  }

  const auto& mode = mModes[0];
  if (mWidthMillimeters) {
    const uint32_t dpi =
        static_cast<uint32_t>((static_cast<float>(mode->hdisplay) /
                               static_cast<float>(*mWidthMillimeters)) *
                              kMillimetersPerInch);
    DEBUG_LOG("%s: connector:%" PRIu32 " has dpi-x:%" PRIu32, __FUNCTION__, mId,
              dpi);
    return dpi;
  }

  return 0;
}

uint32_t DrmConnector::getDpiY() const {
  DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

  if (mModes.empty()) {
    return 0;
  }

  const auto& mode = mModes[0];
  if (mHeightMillimeters) {
    const uint32_t dpi =
        static_cast<uint32_t>((static_cast<float>(mode->vdisplay) /
                               static_cast<float>(*mHeightMillimeters)) *
                              kMillimetersPerInch);
    DEBUG_LOG("%s: connector:%" PRIu32 " has dpi-x:%" PRIu32, __FUNCTION__, mId,
              dpi);
    return dpi;
  }

  return 0;
}

float DrmConnector::getRefreshRate() const {
  DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

  if (!mModes.empty()) {
    const auto& mode = mModes[0];
    return 1000.0f * mode->clock / ((float)mode->vtotal * (float)mode->htotal);
  }

  return -1.0f;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
