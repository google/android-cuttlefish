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

#include "ClientFrameComposer.h"

#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android/hardware/graphics/common/1.0/types.h>
#include <drm_fourcc.h>
#include <libyuv.h>
#include <sync/sync.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>

#include "Display.h"
#include "Drm.h"
#include "Layer.h"

namespace aidl::android::hardware::graphics::composer3::impl {

HWC3::Error ClientFrameComposer::init() {
  DEBUG_LOG("%s", __FUNCTION__);

  HWC3::Error error = mDrmClient.init();
  if (error != HWC3::Error::None) {
    ALOGE("%s: failed to initialize DrmClient", __FUNCTION__);
    return error;
  }

  return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::registerOnHotplugCallback(
    const HotplugCallback& cb) {
  return mDrmClient.registerOnHotplugCallback(cb);
  return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::unregisterOnHotplugCallback() {
  return mDrmClient.unregisterOnHotplugCallback();
}

HWC3::Error ClientFrameComposer::onDisplayCreate(Display* display) {
  const auto displayId = display->getId();
  DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

  // Ensure created.
  mDisplayInfos.emplace(displayId, DisplayInfo{});

  return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::onDisplayDestroy(Display* display) {
  const auto displayId = display->getId();
  DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

  auto it = mDisplayInfos.find(displayId);
  if (it == mDisplayInfos.end()) {
    ALOGE("%s: display:%" PRIu64 " missing display buffers?", __FUNCTION__,
          displayId);
    return HWC3::Error::BadDisplay;
  }

  mDisplayInfos.erase(it);

  return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::onDisplayClientTargetSet(Display* display) {
  const auto displayId = display->getId();
  DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

  auto it = mDisplayInfos.find(displayId);
  if (it == mDisplayInfos.end()) {
    ALOGE("%s: display:%" PRIu64 " missing display buffers?", __FUNCTION__,
          displayId);
    return HWC3::Error::BadDisplay;
  }

  DisplayInfo& displayInfo = it->second;

  auto [drmBufferCreateError, drmBuffer] =
      mDrmClient.create(display->getClientTarget().getBuffer());
  if (drmBufferCreateError != HWC3::Error::None) {
    ALOGE("%s: display:%" PRIu64 " failed to create client target drm buffer",
          __FUNCTION__, displayId);
    return HWC3::Error::NoResources;
  }
  displayInfo.clientTargetDrmBuffer = std::move(drmBuffer);

  return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::onActiveConfigChange(Display* /*display*/) {
  return HWC3::Error::None;
};

HWC3::Error ClientFrameComposer::validateDisplay(Display* display,
                                                 DisplayChanges* outChanges) {
  const auto displayId = display->getId();
  DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

  const std::vector<Layer*>& layers = display->getOrderedLayers();

  for (Layer* layer : layers) {
    const auto layerId = layer->getId();
    const auto layerCompositionType = layer->getCompositionType();

    if (layerCompositionType != Composition::CLIENT) {
      outChanges->addLayerCompositionChange(displayId, layerId,
                                            Composition::CLIENT);
      continue;
    }
  }

  return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::presentDisplay(
    Display* display, ::android::base::unique_fd* outDisplayFence,
    std::unordered_map<int64_t,
                       ::android::base::unique_fd>* /*outLayerFences*/) {
  const auto displayId = display->getId();
  DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

  auto displayInfoIt = mDisplayInfos.find(displayId);
  if (displayInfoIt == mDisplayInfos.end()) {
    ALOGE("%s: failed to find display buffers for display:%" PRIu64,
          __FUNCTION__, displayId);
    return HWC3::Error::BadDisplay;
  }

  DisplayInfo& displayInfo = displayInfoIt->second;
  if (!displayInfo.clientTargetDrmBuffer) {
    ALOGW("%s: display:%" PRIu64 " no client target set, nothing to present.",
          __FUNCTION__, displayId);
    return HWC3::Error::None;
  }

  ::android::base::unique_fd fence = display->getClientTarget().getFence();

  auto [flushError, flushCompleteFence] =
      mDrmClient.flushToDisplay(static_cast<uint32_t>(displayId),
                                displayInfo.clientTargetDrmBuffer, fence);
  if (flushError != HWC3::Error::None) {
    ALOGE("%s: display:%" PRIu64 " failed to flush drm buffer" PRIu64,
          __FUNCTION__, displayId);
  }

  *outDisplayFence = std::move(flushCompleteFence);
  return flushError;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
