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

#ifndef ANDROID_HWC_HOSTFRAMECOMPOSER_H
#define ANDROID_HWC_HOSTFRAMECOMPOSER_H

#include <android-base/unique_fd.h>

#include <optional>
#include <tuple>

#include "Common.h"
#include "DrmClient.h"
#include "DrmSwapchain.h"
#include "FrameComposer.h"
#include "HostConnection.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class HostFrameComposer : public FrameComposer {
 public:
  HostFrameComposer() = default;

  HostFrameComposer(const HostFrameComposer&) = delete;
  HostFrameComposer& operator=(const HostFrameComposer&) = delete;

  HostFrameComposer(HostFrameComposer&&) = delete;
  HostFrameComposer& operator=(HostFrameComposer&&) = delete;

  HWC3::Error init() override;

  HWC3::Error registerOnHotplugCallback(const HotplugCallback& cb) override;

  HWC3::Error unregisterOnHotplugCallback() override;

  HWC3::Error onDisplayCreate(Display* display) override;

  HWC3::Error onDisplayDestroy(Display* display) override;

  HWC3::Error onDisplayClientTargetSet(Display* display) override;

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

  HWC3::Error onActiveConfigChange(Display* display) override;

  const DrmClient* getDrmPresenter() const override {
    if (mDrmClient) {
      return &*mDrmClient;
    }
    return nullptr;
  }

 private:
  HWC3::Error createHostComposerDisplayInfo(Display* display,
                                            uint32_t hostDisplayId);

  void post(HostConnection* hostCon, ExtendedRCEncoderContext* rcEnc,
            uint32_t hostDisplayId, buffer_handle_t h);

  bool mIsMinigbm = false;

  int mSyncDeviceFd = -1;

  struct HostComposerDisplayInfo {
    uint32_t hostDisplayId = 0;
    std::unique_ptr<DrmSwapchain> swapchain = {};
    // Drm info for the displays client target buffer.
    std::shared_ptr<DrmBuffer> clientTargetDrmBuffer;
  };

  std::unique_ptr<gfxstream::SyncHelper> mSyncHelper = nullptr;
  std::unordered_map<int64_t, HostComposerDisplayInfo> mDisplayInfos;

  std::optional<DrmClient> mDrmClient;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
