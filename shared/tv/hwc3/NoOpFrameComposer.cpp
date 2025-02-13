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

#include "NoOpFrameComposer.h"

#include "Display.h"
#include "Drm.h"
#include "Layer.h"

namespace aidl::android::hardware::graphics::composer3::impl {

HWC3::Error NoOpFrameComposer::init() {
  DEBUG_LOG("%s", __FUNCTION__);

  return HWC3::Error::None;
}

HWC3::Error NoOpFrameComposer::registerOnHotplugCallback(
    const HotplugCallback&) {
  DEBUG_LOG("%s", __FUNCTION__);

  return HWC3::Error::None;
}

HWC3::Error NoOpFrameComposer::unregisterOnHotplugCallback() {
  DEBUG_LOG("%s", __FUNCTION__);

  return HWC3::Error::None;
}

HWC3::Error NoOpFrameComposer::onDisplayCreate(Display*) {
  DEBUG_LOG("%s", __FUNCTION__);

  return HWC3::Error::None;
}

HWC3::Error NoOpFrameComposer::onDisplayDestroy(Display*) {
  DEBUG_LOG("%s", __FUNCTION__);

  return HWC3::Error::None;
}

HWC3::Error NoOpFrameComposer::onDisplayClientTargetSet(Display*) {
  DEBUG_LOG("%s", __FUNCTION__);

  return HWC3::Error::None;
}

HWC3::Error NoOpFrameComposer::onActiveConfigChange(Display*) {
  DEBUG_LOG("%s", __FUNCTION__);

  return HWC3::Error::None;
};

HWC3::Error NoOpFrameComposer::validateDisplay(Display*, DisplayChanges*) {
  DEBUG_LOG("%s", __FUNCTION__);

  return HWC3::Error::None;
}

HWC3::Error NoOpFrameComposer::presentDisplay(
    Display*, ::android::base::unique_fd*,
    std::unordered_map<int64_t,
                       ::android::base::unique_fd>* /*outLayerFences*/) {
  DEBUG_LOG("%s", __FUNCTION__);

  return HWC3::Error::None;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
