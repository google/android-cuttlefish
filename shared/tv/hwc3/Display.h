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

#ifndef ANDROID_HWC_DISPLAY_H
#define ANDROID_HWC_DISPLAY_H

#include <aidl/android/hardware/graphics/common/DisplayDecorationSupport.h>
#include <aidl/android/hardware/graphics/composer3/ColorMode.h>
#include <aidl/android/hardware/graphics/composer3/ContentType.h>
#include <aidl/android/hardware/graphics/composer3/DisplayAttribute.h>
#include <aidl/android/hardware/graphics/composer3/DisplayCapability.h>
#include <aidl/android/hardware/graphics/composer3/DisplayConnectionType.h>
#include <aidl/android/hardware/graphics/composer3/DisplayContentSample.h>
#include <aidl/android/hardware/graphics/composer3/DisplayIdentification.h>
#include <aidl/android/hardware/graphics/composer3/HdrCapabilities.h>
#include <aidl/android/hardware/graphics/composer3/OutputType.h>
#include <aidl/android/hardware/graphics/composer3/PerFrameMetadataKey.h>
#include <aidl/android/hardware/graphics/composer3/PowerMode.h>
#include <aidl/android/hardware/graphics/composer3/ReadbackBufferAttributes.h>
#include <aidl/android/hardware/graphics/composer3/RenderIntent.h>
#include <aidl/android/hardware/graphics/composer3/VsyncPeriodChangeConstraints.h>
#include <aidl/android/hardware/graphics/composer3/VsyncPeriodChangeTimeline.h>
#include <android-base/unique_fd.h>

#include <array>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Common.h"
#include "DisplayChanges.h"
#include "DisplayConfig.h"
#include "DisplayFinder.h"
#include "FencedBuffer.h"
#include "FrameComposer.h"
#include "Layer.h"
#include "Time.h"
#include "VsyncThread.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class FrameComposer;

class Display {
 public:
  Display(FrameComposer* composer, int64_t id);
  ~Display();

  Display(const Display& display) = delete;
  Display& operator=(const Display& display) = delete;

  Display(Display&& display) = delete;
  Display& operator=(Display&& display) = delete;

  HWC3::Error init(
      const std::vector<DisplayConfig>& configs, int32_t activeConfigId,
      const std::optional<std::vector<uint8_t>>& edid = std::nullopt);

  HWC3::Error updateParameters(
      uint32_t width, uint32_t height, uint32_t dpiX, uint32_t dpiY,
      uint32_t refreshRateHz,
      const std::optional<std::vector<uint8_t>>& edid = std::nullopt);

  // HWComposer3 interface.
  HWC3::Error createLayer(int64_t* outLayerId);
  HWC3::Error destroyLayer(int64_t layerId);
  HWC3::Error getActiveConfig(int32_t* outConfigId);
  HWC3::Error getDisplayAttribute(int32_t configId, DisplayAttribute attribute,
                                  int32_t* outValue);
  HWC3::Error getColorModes(std::vector<ColorMode>* outColorModes);
  HWC3::Error getDisplayCapabilities(std::vector<DisplayCapability>* caps);
  HWC3::Error getDisplayConfigs(std::vector<int32_t>* configs);
  HWC3::Error getDisplayConfigurations(
      std::vector<DisplayConfiguration>* outConfigs);
  HWC3::Error getDisplayConnectionType(DisplayConnectionType* outType);
  HWC3::Error getDisplayIdentificationData(
      DisplayIdentification* outIdentification);
  HWC3::Error getDisplayName(std::string* outName);
  HWC3::Error getDisplayVsyncPeriod(int32_t* outVsyncPeriod);
  HWC3::Error getDisplayedContentSample(int64_t maxFrames, int64_t timestamp,
                                        DisplayContentSample* samples);
  HWC3::Error getDisplayedContentSamplingAttributes(
      DisplayContentSamplingAttributes* outAttributes);
  HWC3::Error getDisplayPhysicalOrientation(common::Transform* outOrientation);
  HWC3::Error getHdrCapabilities(HdrCapabilities* outCapabilities);
  HWC3::Error getPerFrameMetadataKeys(
      std::vector<PerFrameMetadataKey>* outKeys);
  HWC3::Error getReadbackBufferAttributes(ReadbackBufferAttributes* attrs);
  HWC3::Error getReadbackBufferFence(ndk::ScopedFileDescriptor* acquireFence);
  HWC3::Error getRenderIntents(ColorMode mode,
                               std::vector<RenderIntent>* intents);
  HWC3::Error getSupportedContentTypes(std::vector<ContentType>* types);
  HWC3::Error getDecorationSupport(
      std::optional<common::DisplayDecorationSupport>* support);
  HWC3::Error registerCallback(
      const std::shared_ptr<IComposerCallback>& callback);
  HWC3::Error setActiveConfig(int32_t configId);
  HWC3::Error setActiveConfigWithConstraints(
      int32_t config, const VsyncPeriodChangeConstraints& constraints,
      VsyncPeriodChangeTimeline* outTimeline);
  HWC3::Error setBootConfig(int32_t configId);
  HWC3::Error clearBootConfig();
  HWC3::Error getPreferredBootConfig(int32_t* outConfigId);
  HWC3::Error setAutoLowLatencyMode(bool on);
  HWC3::Error setColorMode(ColorMode mode, RenderIntent intent);
  HWC3::Error setContentType(ContentType contentType);
  HWC3::Error setDisplayedContentSamplingEnabled(
      bool enable, FormatColorComponent componentMask, int64_t maxFrames);
  HWC3::Error setPowerMode(PowerMode mode);
  HWC3::Error setReadbackBuffer(const buffer_handle_t buffer,
                                const ndk::ScopedFileDescriptor& releaseFence);
  HWC3::Error setVsyncEnabled(bool enabled);
  HWC3::Error setIdleTimerEnabled(int32_t timeoutMs);
  HWC3::Error setColorTransform(const std::vector<float>& transform);
  HWC3::Error setBrightness(float brightness);
  HWC3::Error setClientTarget(buffer_handle_t buffer,
                              const ndk::ScopedFileDescriptor& fence,
                              common::Dataspace dataspace,
                              const std::vector<common::Rect>& damage);
  HWC3::Error setOutputBuffer(buffer_handle_t buffer,
                              const ndk::ScopedFileDescriptor& fence);
  HWC3::Error setExpectedPresentTime(
      const std::optional<ClockMonotonicTimestamp>& expectedPresentTime);
  HWC3::Error validate(DisplayChanges* outChanges);
  HWC3::Error acceptChanges();
  HWC3::Error present(
      ::android::base::unique_fd* outDisplayFence,
      std::unordered_map<int64_t, ::android::base::unique_fd>* outLayerFences);

  // Non HWCComposer3 interface.
  int64_t getId() const { return mId; }

  Layer* getLayer(int64_t layerHandle);

  HWC3::Error setEdid(std::vector<uint8_t> edid);

  bool hasColorTransform() const { return mColorTransform.has_value(); }
  std::array<float, 16> getColorTransform() const { return *mColorTransform; }

  FencedBuffer& getClientTarget() { return mClientTarget; }
  buffer_handle_t waitAndGetClientTargetBuffer();

  const std::vector<Layer*>& getOrderedLayers() { return mOrderedLayers; }

 private:
  bool hasConfig(int32_t configId) const;
  DisplayConfig* getConfig(int32_t configId);

  std::optional<int32_t> getBootConfigId();

  void setLegacyEdid();

  // The state of this display should only be modified from
  // SurfaceFlinger's main loop, with the exception of when dump is
  // called. To prevent a bad state from crashing us during a dump
  // call, all public calls into Display must acquire this mutex.
  mutable std::recursive_mutex mStateMutex;

  FrameComposer* mComposer = nullptr;
  const int64_t mId;
  std::string mName;
  PowerMode mPowerMode = PowerMode::OFF;
  VsyncThread mVsyncThread;
  FencedBuffer mClientTarget;
  FencedBuffer mReadbackBuffer;
  // Will only be non-null after the Display has been validated and
  // before it has been accepted.
  enum class PresentFlowState {
    WAITING_FOR_VALIDATE,
    WAITING_FOR_ACCEPT,
    WAITING_FOR_PRESENT,
  };
  PresentFlowState mPresentFlowState = PresentFlowState::WAITING_FOR_VALIDATE;
  DisplayChanges mPendingChanges;
  std::optional<TimePoint> mExpectedPresentTime;
  std::unordered_map<int64_t, std::unique_ptr<Layer>> mLayers;
  // Ordered layers available after validate().
  std::vector<Layer*> mOrderedLayers;
  std::optional<int32_t> mActiveConfigId;
  std::unordered_map<int32_t, DisplayConfig> mConfigs;
  std::unordered_set<ColorMode> mColorModes = {ColorMode::NATIVE};
  ColorMode mActiveColorMode = ColorMode::NATIVE;
  std::optional<std::array<float, 16>> mColorTransform;
  std::vector<uint8_t> mEdid;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
