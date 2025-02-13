/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef ANDROID_HWC_COMPOSERCLIENT_H
#define ANDROID_HWC_COMPOSERCLIENT_H

#include <aidl/android/hardware/graphics/composer3/BnComposerClient.h>
#include <aidl/android/hardware/graphics/composer3/Luts.h>
#include <android-base/thread_annotations.h>

#include <memory>

#include "ComposerResources.h"
#include "Display.h"
#include "FrameComposer.h"
#include "PictureProfileChangedListener.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class ComposerClient : public BnComposerClient {
 public:
  ComposerClient();
  virtual ~ComposerClient();

  HWC3::Error init();

  void setOnClientDestroyed(std::function<void()> onClientDestroyed) {
    mOnClientDestroyed = onClientDestroyed;
  }

  // HWC3 interface:
  ndk::ScopedAStatus createLayer(int64_t displayId, int32_t bufferSlotCount,
                                 int64_t* layer) override;
  ndk::ScopedAStatus createVirtualDisplay(int32_t width, int32_t height,
                                          common::PixelFormat formatHint,
                                          int32_t outputBufferSlotCount,
                                          VirtualDisplay* display) override;
  ndk::ScopedAStatus destroyLayer(int64_t displayId, int64_t layer) override;
  ndk::ScopedAStatus destroyVirtualDisplay(int64_t displayId) override;
  ndk::ScopedAStatus executeCommands(
      const std::vector<DisplayCommand>& commands,
      std::vector<CommandResultPayload>* results) override;
  ndk::ScopedAStatus getActiveConfig(int64_t displayId,
                                     int32_t* config) override;
  ndk::ScopedAStatus getColorModes(int64_t displayId,
                                   std::vector<ColorMode>* colorModes) override;
  ndk::ScopedAStatus getDataspaceSaturationMatrix(
      common::Dataspace dataspace, std::vector<float>* matrix) override;
  ndk::ScopedAStatus getDisplayAttribute(int64_t displayId, int32_t config,
                                         DisplayAttribute attribute,
                                         int32_t* value) override;
  ndk::ScopedAStatus getDisplayCapabilities(
      int64_t displayId, std::vector<DisplayCapability>* caps) override;
  ndk::ScopedAStatus getDisplayConfigs(int64_t displayId,
                                       std::vector<int32_t>* configs) override;
  ndk::ScopedAStatus getDisplayConnectionType(
      int64_t displayId, DisplayConnectionType* type) override;
  ndk::ScopedAStatus getDisplayIdentificationData(
      int64_t displayId, DisplayIdentification* id) override;
  ndk::ScopedAStatus getDisplayName(int64_t displayId,
                                    std::string* name) override;
  ndk::ScopedAStatus getDisplayVsyncPeriod(int64_t displayId,
                                           int32_t* vsyncPeriod) override;
  ndk::ScopedAStatus getDisplayedContentSample(
      int64_t displayId, int64_t maxFrames, int64_t timestamp,
      DisplayContentSample* samples) override;
  ndk::ScopedAStatus getDisplayedContentSamplingAttributes(
      int64_t displayId, DisplayContentSamplingAttributes* attrs) override;
  ndk::ScopedAStatus getDisplayPhysicalOrientation(
      int64_t displayId, common::Transform* orientation) override;
  ndk::ScopedAStatus getHdrCapabilities(int64_t displayId,
                                        HdrCapabilities* caps) override;
  ndk::ScopedAStatus getOverlaySupport(OverlayProperties* properties) override;
  ndk::ScopedAStatus getMaxVirtualDisplayCount(int32_t* count) override;
  ndk::ScopedAStatus getPerFrameMetadataKeys(
      int64_t displayId, std::vector<PerFrameMetadataKey>* keys) override;
  ndk::ScopedAStatus getReadbackBufferAttributes(
      int64_t displayId, ReadbackBufferAttributes* attrs) override;
  ndk::ScopedAStatus getReadbackBufferFence(
      int64_t displayId, ndk::ScopedFileDescriptor* acquireFence) override;
  ndk::ScopedAStatus getRenderIntents(
      int64_t displayId, ColorMode mode,
      std::vector<RenderIntent>* intents) override;
  ndk::ScopedAStatus getSupportedContentTypes(
      int64_t displayId, std::vector<ContentType>* types) override;
  ndk::ScopedAStatus getDisplayDecorationSupport(
      int64_t displayId,
      std::optional<common::DisplayDecorationSupport>* support) override;
  ndk::ScopedAStatus registerCallback(
      const std::shared_ptr<IComposerCallback>& callback) override;
  ndk::ScopedAStatus setActiveConfig(int64_t displayId,
                                     int32_t config) override;
  ndk::ScopedAStatus setActiveConfigWithConstraints(
      int64_t displayId, int32_t config,
      const VsyncPeriodChangeConstraints& constraints,
      VsyncPeriodChangeTimeline* timeline) override;
  ndk::ScopedAStatus setBootDisplayConfig(int64_t displayId,
                                          int32_t config) override;
  ndk::ScopedAStatus clearBootDisplayConfig(int64_t displayId) override;
  ndk::ScopedAStatus getPreferredBootDisplayConfig(int64_t displayId,
                                                   int32_t* config) override;
  ndk::ScopedAStatus getHdrConversionCapabilities(
      std::vector<
          aidl::android::hardware::graphics::common::HdrConversionCapability>*)
      override;
  ndk::ScopedAStatus setHdrConversionStrategy(
      const aidl::android::hardware::graphics::common::HdrConversionStrategy&
          conversionStrategy,
      aidl::android::hardware::graphics::common::Hdr* preferredHdrOutputType)
      override;
  ndk::ScopedAStatus setAutoLowLatencyMode(int64_t displayId, bool on) override;
  ndk::ScopedAStatus setClientTargetSlotCount(int64_t displayId,
                                              int32_t count) override;
  ndk::ScopedAStatus setColorMode(int64_t displayId, ColorMode mode,
                                  RenderIntent intent) override;
  ndk::ScopedAStatus setContentType(int64_t displayId,
                                    ContentType type) override;
  ndk::ScopedAStatus setDisplayedContentSamplingEnabled(
      int64_t displayId, bool enable, FormatColorComponent componentMask,
      int64_t maxFrames) override;
  ndk::ScopedAStatus setPowerMode(int64_t displayId, PowerMode mode) override;
  ndk::ScopedAStatus setReadbackBuffer(
      int64_t displayId,
      const aidl::android::hardware::common::NativeHandle& buffer,
      const ndk::ScopedFileDescriptor& releaseFence) override;
  ndk::ScopedAStatus setVsyncEnabled(int64_t displayId, bool enabled) override;
  ndk::ScopedAStatus setIdleTimerEnabled(int64_t displayId,
                                         int32_t timeoutMs) override;
  ndk::ScopedAStatus setRefreshRateChangedCallbackDebugEnabled(
      int64_t displayId, bool enabled) override;
  ndk::ScopedAStatus getDisplayConfigurations(
      int64_t displayId, int32_t maxFrameIntervalNs,
      std::vector<DisplayConfiguration>*) override;
  ndk::ScopedAStatus notifyExpectedPresent(
      int64_t displayId, const ClockMonotonicTimestamp& expectedPresentTime,
      int32_t maxFrameIntervalNs) override;
  ndk::ScopedAStatus getMaxLayerPictureProfiles(
      int64_t displayId, int32_t* outMaxProfiles) override;
  ndk::ScopedAStatus startHdcpNegotiation(
      int64_t displayId,
      const aidl::android::hardware::drm::HdcpLevels& levels) override;
  ndk::ScopedAStatus getLuts(int64_t displayId, const std::vector<Buffer>&,
                             std::vector<Luts>*) override;
  void setPictureProfileChangedListener(
      std::shared_ptr<PictureProfileChangedListener>
          pictureProfileChangedListener);

 protected:
  ndk::SpAIBinder createBinder() override;

 private:
  class CommandResultWriter;

  void executeDisplayCommand(CommandResultWriter& commandResults,
                             const DisplayCommand& displayCommand);

  void executeLayerCommand(CommandResultWriter& commandResults,
                           Display& display, const LayerCommand& layerCommand);

  void executeDisplayCommandSetColorTransform(
      CommandResultWriter& commandResults, Display& display,
      const std::vector<float>& matrix);
  void executeDisplayCommandSetBrightness(CommandResultWriter& commandResults,
                                          Display& display,
                                          const DisplayBrightness& brightness);
  void executeDisplayCommandSetClientTarget(CommandResultWriter& commandResults,
                                            Display& display,
                                            const ClientTarget& command);
  void executeDisplayCommandSetOutputBuffer(CommandResultWriter& commandResults,
                                            Display& display,
                                            const Buffer& buffer);
  void executeDisplayCommandValidateDisplay(
      CommandResultWriter& commandResults, Display& display,
      const std::optional<ClockMonotonicTimestamp> expectedPresentTime);
  void executeDisplayCommandAcceptDisplayChanges(
      CommandResultWriter& commandResults, Display& display);
  void executeDisplayCommandPresentOrValidateDisplay(
      CommandResultWriter& commandResults, Display& display,
      const std::optional<ClockMonotonicTimestamp> expectedPresentTime);
  void executeDisplayCommandPresentDisplay(CommandResultWriter& commandResults,
                                           Display& display);

  void executeLayerCommandSetLayerCursorPosition(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const common::Point& cursorPosition);
  void executeLayerCommandSetLayerBuffer(CommandResultWriter& commandResults,
                                         Display& display, Layer* layer,
                                         const Buffer& buffer);
  void executeLayerCommandSetLayerSurfaceDamage(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const std::vector<std::optional<common::Rect>>& damage);
  void executeLayerCommandSetLayerBlendMode(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const ParcelableBlendMode& blendMode);
  void executeLayerCommandSetLayerColor(CommandResultWriter& commandResults,
                                        Display& display, Layer* layer,
                                        const Color& color);
  void executeLayerCommandSetLayerComposition(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const ParcelableComposition& composition);
  void executeLayerCommandSetLayerDataspace(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const ParcelableDataspace& dataspace);
  void executeLayerCommandSetLayerDisplayFrame(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const common::Rect& rect);
  void executeLayerCommandSetLayerPlaneAlpha(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const PlaneAlpha& planeAlpha);
  void executeLayerCommandSetLayerSidebandStream(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const aidl::android::hardware::common::NativeHandle& sidebandStream);
  void executeLayerCommandSetLayerSourceCrop(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const common::FRect& sourceCrop);
  void executeLayerCommandSetLayerTransform(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const ParcelableTransform& transform);
  void executeLayerCommandSetLayerVisibleRegion(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const std::vector<std::optional<common::Rect>>& visibleRegion);
  void executeLayerCommandSetLayerZOrder(CommandResultWriter& commandResults,
                                         Display& display, Layer* layer,
                                         const ZOrder& zOrder);
  void executeLayerCommandSetLayerPerFrameMetadata(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const std::vector<std::optional<PerFrameMetadata>>& perFrameMetadata);
  void executeLayerCommandSetLayerColorTransform(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const std::vector<float>& colorTransform);
  void executeLayerCommandSetLayerBrightness(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const LayerBrightness& brightness);
  void executeLayerCommandSetLayerPerFrameMetadataBlobs(
      CommandResultWriter& commandResults, Display& display, Layer* layer,
      const std::vector<std::optional<PerFrameMetadataBlob>>&
          perFrameMetadataBlob);
  void executeLayerCommandSetLayerLuts(CommandResultWriter& commandResults,
                                       Display& display, Layer* layer,
                                       const Luts& luts);

  // Returns the display with the given id or nullptr if not found.
  std::shared_ptr<Display> getDisplay(int64_t displayId);

  // Finds the Cuttlefish/Goldfish specific configuration and initializes the
  // displays.
  HWC3::Error createDisplaysLocked() EXCLUSIVE_LOCKS_REQUIRED(mDisplaysMutex);

  // Creates a display with the given properties.
  HWC3::Error createDisplayLocked(int64_t displayId, int32_t activeConfigId,
                                  const std::vector<DisplayConfig>& configs)
      EXCLUSIVE_LOCKS_REQUIRED(mDisplaysMutex);

  HWC3::Error destroyDisplaysLocked() EXCLUSIVE_LOCKS_REQUIRED(mDisplaysMutex);

  HWC3::Error destroyDisplayLocked(int64_t displayId)
      EXCLUSIVE_LOCKS_REQUIRED(mDisplaysMutex);

  HWC3::Error handleHotplug(bool connected,   //
                            uint32_t id,      //
                            uint32_t width,   //
                            uint32_t height,  //
                            uint32_t dpiX,    //
                            uint32_t dpiY,    //
                            uint32_t refreshRate);

  std::mutex mDisplaysMutex;
  std::map<int64_t, std::shared_ptr<Display>> mDisplays
      GUARDED_BY(mDisplaysMutex);

  // The onHotplug(), onVsync(), etc callbacks registered by SurfaceFlinger.
  std::shared_ptr<IComposerCallback> mCallbacks;

  std::function<void()> mOnClientDestroyed;

  // Underlying interface for composing layers in the guest using libyuv or in
  // the host using opengl. Owned by Device.
  FrameComposer* mComposer = nullptr;

  // Manages importing and caching gralloc buffers for displays and layers.
  std::unique_ptr<ComposerResources> mResources;

  std::shared_ptr<PictureProfileChangedListener> mPictureProfileChangedListener;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
