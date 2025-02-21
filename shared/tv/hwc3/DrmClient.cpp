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

#include "DrmClient.h"

#include <cros_gralloc_handle.h>

using ::gfxstream::guest::AutoReadLock;
using ::gfxstream::guest::AutoWriteLock;
using ::gfxstream::guest::ReadWriteLock;

namespace aidl::android::hardware::graphics::composer3::impl {

DrmClient::~DrmClient() {
  if (mFd > 0) {
    drmDropMaster(mFd.get());
  }
}

::android::base::unique_fd DrmClient::OpenVirtioGpuDrmFd() {
  for (int i = 0; i < 10; i++) {
    const std::string path = "/dev/dri/card" + std::to_string(i);
    DEBUG_LOG("%s: trying to open DRM device at %s", __FUNCTION__,
              path.c_str());

    ::android::base::unique_fd fd(open(path.c_str(), O_RDWR | O_CLOEXEC));

    if (fd < 0) {
      ALOGE("%s: failed to open drm device %s: %s", __FUNCTION__, path.c_str(),
            strerror(errno));
      continue;
    }

    auto version = drmGetVersion(fd.get());
    const std::string name = version->name;
    drmFreeVersion(version);

    DEBUG_LOG("%s: The DRM device at %s is \"%s\"", __FUNCTION__, path.c_str(),
              name.c_str());
    if (name.find("virtio") != std::string::npos) {
      return fd;
    }
  }

  ALOGE(
      "Failed to find virtio-gpu DRM node. Ranchu HWComposer "
      "is only expected to be used with \"virtio_gpu\"");

  return ::android::base::unique_fd(-1);
}

HWC3::Error DrmClient::init() {
  DEBUG_LOG("%s", __FUNCTION__);

  mFd = OpenVirtioGpuDrmFd();
  if (mFd < 0) {
    ALOGE("%s: failed to open drm device: %s", __FUNCTION__, strerror(errno));
    return HWC3::Error::NoResources;
  }

  int ret = drmSetClientCap(mFd.get(), DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (ret) {
    ALOGE("%s: failed to set cap universal plane %s\n", __FUNCTION__,
          strerror(errno));
    return HWC3::Error::NoResources;
  }

  ret = drmSetClientCap(mFd.get(), DRM_CLIENT_CAP_ATOMIC, 1);
  if (ret) {
    ALOGE("%s: failed to set cap atomic %s\n", __FUNCTION__, strerror(errno));
    return HWC3::Error::NoResources;
  }

  drmSetMaster(mFd.get());

  if (!drmIsMaster(mFd.get())) {
    ALOGE("%s: failed to get master drm device", __FUNCTION__);
    return HWC3::Error::NoResources;
  }

  {
    AutoWriteLock lock(mDisplaysMutex);
    bool success = loadDrmDisplays();
    if (success) {
      DEBUG_LOG("%s: Successfully initialized DRM backend", __FUNCTION__);
    } else {
      ALOGE("%s: Failed to initialize DRM backend", __FUNCTION__);
      return HWC3::Error::NoResources;
    }
  }

  mDrmEventListener =
      DrmEventListener::create(mFd, [this]() { handleHotplug(); });
  if (!mDrmEventListener) {
    ALOGE("%s: Failed to initialize DRM event listener", __FUNCTION__);
  } else {
    DEBUG_LOG("%s: Successfully initialized DRM event listener", __FUNCTION__);
  }

  DEBUG_LOG("%s: Successfully initialized.", __FUNCTION__);
  return HWC3::Error::None;
}

HWC3::Error DrmClient::getDisplayConfigs(
    std::vector<DisplayConfig>* configs) const {
  DEBUG_LOG("%s", __FUNCTION__);

  AutoReadLock lock(mDisplaysMutex);

  configs->clear();

  for (const auto& display : mDisplays) {
    if (!display->isConnected()) {
      continue;
    }

    configs->emplace_back(DisplayConfig{
        .id = display->getId(),
        .width = display->getWidth(),
        .height = display->getHeight(),
        .dpiX = display->getDpiX(),
        .dpiY = display->getDpiY(),
        .refreshRateHz = display->getRefreshRateUint(),
    });
  }

  return HWC3::Error::None;
}

HWC3::Error DrmClient::registerOnHotplugCallback(const HotplugCallback& cb) {
  mHotplugCallback = cb;
  return HWC3::Error::None;
}

HWC3::Error DrmClient::unregisterOnHotplugCallback() {
  mHotplugCallback.reset();
  return HWC3::Error::None;
}

bool DrmClient::loadDrmDisplays() {
  DEBUG_LOG("%s", __FUNCTION__);

  std::vector<std::unique_ptr<DrmCrtc>> crtcs;
  std::vector<std::unique_ptr<DrmConnector>> connectors;
  std::vector<std::unique_ptr<DrmPlane>> planes;

  drmModePlaneResPtr drmPlaneResources = drmModeGetPlaneResources(mFd.get());
  for (uint32_t i = 0; i < drmPlaneResources->count_planes; ++i) {
    const uint32_t planeId = drmPlaneResources->planes[i];

    auto crtc = DrmPlane::create(mFd, planeId);
    if (!crtc) {
      ALOGE("%s: Failed to create DRM CRTC.", __FUNCTION__);
      return false;
    }

    planes.emplace_back(std::move(crtc));
  }
  drmModeFreePlaneResources(drmPlaneResources);

  drmModeRes* drmResources = drmModeGetResources(mFd.get());
  for (uint32_t crtcIndex = 0; crtcIndex < drmResources->count_crtcs;
       crtcIndex++) {
    const uint32_t crtcId = drmResources->crtcs[crtcIndex];

    auto crtc = DrmCrtc::create(mFd, crtcId, crtcIndex);
    if (!crtc) {
      ALOGE("%s: Failed to create DRM CRTC.", __FUNCTION__);
      return false;
    }

    crtcs.emplace_back(std::move(crtc));
  }

  for (uint32_t i = 0; i < drmResources->count_connectors; ++i) {
    const uint32_t connectorId = drmResources->connectors[i];

    auto connector = DrmConnector::create(mFd, connectorId);
    if (!connector) {
      ALOGE("%s: Failed to create DRM CRTC.", __FUNCTION__);
      return false;
    }

    connectors.emplace_back(std::move(connector));
  }

  drmModeFreeResources(drmResources);

  if (crtcs.size() != connectors.size()) {
    ALOGE(
        "%s: Failed assumption mCrtcs.size():%zu equals mConnectors.size():%zu",
        __FUNCTION__, crtcs.size(), connectors.size());
    return false;
  }

  for (uint32_t i = 0; i < crtcs.size(); i++) {
    std::unique_ptr<DrmCrtc> crtc = std::move(crtcs[i]);
    std::unique_ptr<DrmConnector> connector = std::move(connectors[i]);

    auto planeIt =
        std::find_if(planes.begin(), planes.end(),
                     [&](const std::unique_ptr<DrmPlane>& plane) {
                       if (!plane->isOverlay() && !plane->isPrimary()) {
                         return false;
                       }
                       return plane->isCompatibleWith(*crtc);
                     });
    if (planeIt == planes.end()) {
      ALOGE("%s: Failed to find plane for display:%" PRIu32, __FUNCTION__, i);
      return false;
    }

    std::unique_ptr<DrmPlane> plane = std::move(*planeIt);
    planes.erase(planeIt);

    auto display = DrmDisplay::create(i, std::move(connector), std::move(crtc),
                                      std::move(plane), mFd);
    if (!display) {
      return false;
    }
    mDisplays.push_back(std::move(display));
  }

  return true;
}

std::tuple<HWC3::Error, std::shared_ptr<DrmBuffer>> DrmClient::create(
    const native_handle_t* handle) {
  cros_gralloc_handle* crosHandle = (cros_gralloc_handle*)handle;
  if (crosHandle == nullptr) {
    ALOGE("%s: invalid cros_gralloc_handle", __FUNCTION__);
    return std::make_tuple(HWC3::Error::NoResources, nullptr);
  }

  DrmPrimeBufferHandle primeHandle = 0;
  int ret = drmPrimeFDToHandle(mFd.get(), crosHandle->fds[0], &primeHandle);
  if (ret) {
    ALOGE("%s: drmPrimeFDToHandle failed: %s (errno %d)", __FUNCTION__,
          strerror(errno), errno);
    return std::make_tuple(HWC3::Error::NoResources, nullptr);
  }

  auto buffer = std::shared_ptr<DrmBuffer>(new DrmBuffer(*this));
  buffer->mWidth = crosHandle->width;
  buffer->mHeight = crosHandle->height;
  buffer->mDrmFormat = crosHandle->format;
  buffer->mPlaneFds[0] = crosHandle->fds[0];
  buffer->mPlaneHandles[0] = primeHandle;
  buffer->mPlanePitches[0] = crosHandle->strides[0];
  buffer->mPlaneOffsets[0] = crosHandle->offsets[0];

  uint32_t framebuffer = 0;
  ret = drmModeAddFB2(mFd.get(), buffer->mWidth, buffer->mHeight,
                      buffer->mDrmFormat, buffer->mPlaneHandles,
                      buffer->mPlanePitches, buffer->mPlaneOffsets,
                      &framebuffer, 0);
  if (ret) {
    ALOGE("%s: drmModeAddFB2 failed: %s (errno %d)", __FUNCTION__,
          strerror(errno), errno);
    return std::make_tuple(HWC3::Error::NoResources, nullptr);
  }
  DEBUG_LOG("%s: created framebuffer:%" PRIu32, __FUNCTION__, framebuffer);
  buffer->mDrmFramebuffer = framebuffer;

  return std::make_tuple(HWC3::Error::None, std::shared_ptr<DrmBuffer>(buffer));
}

HWC3::Error DrmClient::destroyDrmFramebuffer(DrmBuffer* buffer) {
  if (buffer->mDrmFramebuffer) {
    uint32_t framebuffer = *buffer->mDrmFramebuffer;
    if (drmModeRmFB(mFd.get(), framebuffer)) {
      ALOGE("%s: drmModeRmFB failed: %s (errno %d)", __FUNCTION__,
            strerror(errno), errno);
      return HWC3::Error::NoResources;
    }
    DEBUG_LOG("%s: destroyed framebuffer:%" PRIu32, __FUNCTION__, framebuffer);
    buffer->mDrmFramebuffer.reset();
  }
  if (buffer->mPlaneHandles[0]) {
    struct drm_gem_close gem_close = {};
    gem_close.handle = buffer->mPlaneHandles[0];
    if (drmIoctl(mFd.get(), DRM_IOCTL_GEM_CLOSE, &gem_close)) {
      ALOGE("%s: DRM_IOCTL_GEM_CLOSE failed: %s (errno %d)", __FUNCTION__,
            strerror(errno), errno);
      return HWC3::Error::NoResources;
    }
  }

  return HWC3::Error::None;
}

bool DrmClient::handleHotplug() {
  DEBUG_LOG("%s", __FUNCTION__);

  struct HotplugToReport {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t dpiX;
    uint32_t dpiY;
    uint32_t rr;
    bool connected;
  };

  std::vector<HotplugToReport> hotplugs;

  {
    AutoWriteLock lock(mDisplaysMutex);

    for (auto& display : mDisplays) {
      auto change = display->checkAndHandleHotplug(mFd);
      if (change == DrmHotplugChange::kNoChange) {
        continue;
      }

      hotplugs.push_back(HotplugToReport{
          .id = display->getId(),
          .width = display->getWidth(),
          .height = display->getHeight(),
          .dpiX = display->getDpiX(),
          .dpiY = display->getDpiY(),
          .rr = display->getRefreshRateUint(),
          .connected = change == DrmHotplugChange::kConnected,
      });
    }
  }

  for (const auto& hotplug : hotplugs) {
    if (mHotplugCallback) {
      (*mHotplugCallback)(hotplug.connected,  //
                          hotplug.id,         //
                          hotplug.width,      //
                          hotplug.height,     //
                          hotplug.dpiX,       //
                          hotplug.dpiY,       //
                          hotplug.rr);
    }
  }

  return true;
}

std::tuple<HWC3::Error, ::android::base::unique_fd> DrmClient::flushToDisplay(
    uint32_t displayId, const std::shared_ptr<DrmBuffer>& buffer,
    ::android::base::borrowed_fd inSyncFd) {
  ATRACE_CALL();

  if (!buffer->mDrmFramebuffer) {
    ALOGE("%s: failed, no framebuffer created.", __FUNCTION__);
    return std::make_tuple(HWC3::Error::NoResources,
                           ::android::base::unique_fd());
  }

  AutoReadLock lock(mDisplaysMutex);
  return mDisplays[displayId]->flush(mFd, inSyncFd, buffer);
}

std::optional<std::vector<uint8_t>> DrmClient::getEdid(uint32_t displayId) {
  AutoReadLock lock(mDisplaysMutex);

  if (displayId >= mDisplays.size()) {
    DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
    return std::nullopt;
  }

  return mDisplays[displayId]->getEdid();
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
