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

#ifndef ANDROID_HWC_NOOPFRAMECOMPOSER_H
#define ANDROID_HWC_NOOPFRAMECOMPOSER_H

#include "Common.h"
#include "Display.h"
#include "DrmClient.h"
#include "FrameComposer.h"
#include "Gralloc.h"
#include "Layer.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class NoOpFrameComposer : public FrameComposer {
 public:
  NoOpFrameComposer() = default;

  NoOpFrameComposer(const NoOpFrameComposer&) = delete;
  NoOpFrameComposer& operator=(const NoOpFrameComposer&) = delete;

  NoOpFrameComposer(NoOpFrameComposer&&) = delete;
  NoOpFrameComposer& operator=(NoOpFrameComposer&&) = delete;

  HWC3::Error init() override;

  HWC3::Error registerOnHotplugCallback(const HotplugCallback& cb) override;

  HWC3::Error unregisterOnHotplugCallback() override;

  HWC3::Error onDisplayCreate(Display*) override;

  HWC3::Error onDisplayDestroy(Display*) override;

  HWC3::Error onDisplayClientTargetSet(Display*) override;

  // Determines if this composer can compose the given layers on the given
  // display and requests changes for layers that can't not be composed.
  HWC3::Error validateDisplay(Display* display,
                              DisplayChanges* outChanges) override;

  // Performs the actual composition of layers and presents the composed result
  // to the display.
  HWC3::Error presentDisplay(
      Display* display, ::android::base::unique_fd* outDisplayFence,
      std::unordered_map<int64_t, ::android::base::unique_fd>* outLayerFences)
      override;

  HWC3::Error onActiveConfigChange(Display* /*display*/) override;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
