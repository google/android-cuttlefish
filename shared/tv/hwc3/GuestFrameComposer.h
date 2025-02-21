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

#ifndef ANDROID_HWC_GUESTFRAMECOMPOSER_H
#define ANDROID_HWC_GUESTFRAMECOMPOSER_H

#include "AlternatingImageStorage.h"
#include "Common.h"
#include "Display.h"
#include "DrmClient.h"
#include "DrmSwapchain.h"
#include "FrameComposer.h"
#include "Gralloc.h"
#include "Layer.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class GuestFrameComposer : public FrameComposer {
 public:
  GuestFrameComposer() = default;

  GuestFrameComposer(const GuestFrameComposer&) = delete;
  GuestFrameComposer& operator=(const GuestFrameComposer&) = delete;

  GuestFrameComposer(GuestFrameComposer&&) = delete;
  GuestFrameComposer& operator=(GuestFrameComposer&&) = delete;

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

  const DrmClient* getDrmPresenter() const override { return &mDrmClient; }

 private:
  struct DisplayConfig {
    int width;
    int height;
    int dpiX;
    int dpiY;
    int refreshRateHz;
  };

  HWC3::Error getDisplayConfigsFromSystemProp(
      std::vector<DisplayConfig>* configs);

  // Returns true if the given layer's buffer has supported format.
  bool canComposeLayer(Layer* layer);

  // Composes the given layer into the given destination buffer.
  HWC3::Error composeLayerInto(AlternatingImageStorage& storage, Layer* layer,
                               std::uint8_t* dstBuffer,
                               std::uint32_t dstBufferWidth,
                               std::uint32_t dstBufferHeight,
                               std::uint32_t dstBufferStrideBytes,
                               std::uint32_t dstBufferBytesPerPixel);

  struct DisplayInfo {
    // Additional per display buffers for the composition result.
    std::unique_ptr<DrmSwapchain> swapchain = {};

    // Scratch storage space for intermediate images during composition.
    AlternatingImageStorage compositionIntermediateStorage;
  };

  std::unordered_map<int64_t, DisplayInfo> mDisplayInfos;

  Gralloc mGralloc;

  DrmClient mDrmClient;

  // Cuttlefish on QEMU does not have a display. Disable presenting to avoid
  // spamming logcat with DRM commit failures.
  bool mPresentDisabled = false;

  HWC3::Error applyColorTransformToRGBA(
      const std::array<float, 16>& colorTransform,  //
      std::uint8_t* buffer,                         //
      std::uint32_t bufferWidth,                    //
      std::uint32_t bufferHeight,                   //
      std::uint32_t bufferStrideBytes);
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
