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

#include "ComposerResources.h"

#include <aidlcommonsupport/NativeHandle.h>

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

HWC3::Error toHwc3Error(
    ::android::hardware::graphics::composer::V2_1::Error error) {
  switch (error) {
    case ::android::hardware::graphics::composer::V2_1::Error::NONE:
      return HWC3::Error::None;
    case ::android::hardware::graphics::composer::V2_1::Error::BAD_CONFIG:
      return HWC3::Error::BadConfig;
    case ::android::hardware::graphics::composer::V2_1::Error::BAD_DISPLAY:
      return HWC3::Error::BadDisplay;
    case ::android::hardware::graphics::composer::V2_1::Error::BAD_LAYER:
      return HWC3::Error::BadLayer;
    case ::android::hardware::graphics::composer::V2_1::Error::BAD_PARAMETER:
      return HWC3::Error::BadParameter;
    case ::android::hardware::graphics::composer::V2_1::Error::NO_RESOURCES:
      return HWC3::Error::NoResources;
    case ::android::hardware::graphics::composer::V2_1::Error::NOT_VALIDATED:
      return HWC3::Error::NotValidated;
    case ::android::hardware::graphics::composer::V2_1::Error::UNSUPPORTED:
      return HWC3::Error::Unsupported;
  }
}

::android::hardware::graphics::composer::V2_1::Display toHwc2Display(
    int64_t displayId) {
  return static_cast<::android::hardware::graphics::composer::V2_1::Display>(
      displayId);
}

::android::hardware::graphics::composer::V2_1::Layer toHwc2Layer(
    int64_t layerId) {
  return static_cast<::android::hardware::graphics::composer::V2_1::Layer>(
      layerId);
}

}  // namespace

std::unique_ptr<ComposerResourceReleaser> ComposerResources::createReleaser(
    bool isBuffer) {
  return std::make_unique<ComposerResourceReleaser>(isBuffer);
}

HWC3::Error ComposerResources::init() {
  mImpl = ::android::hardware::graphics::composer::V2_2::hal::
      ComposerResources::create();
  if (!mImpl) {
    ALOGE("%s: failed to create underlying ComposerResources.", __FUNCTION__);
    return HWC3::Error::NoResources;
  }
  return HWC3::Error::None;
}

void ComposerResources::clear(
    ::android::hardware::graphics::composer::V2_2::hal::ComposerResources::
        RemoveDisplay removeDisplay) {
  mImpl->clear(removeDisplay);
}

bool ComposerResources::hasDisplay(int64_t displayId) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  return mImpl->hasDisplay(display);
}

HWC3::Error ComposerResources::addPhysicalDisplay(int64_t displayId) {
  DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, displayId);
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  return toHwc3Error(mImpl->addPhysicalDisplay(display));
}

HWC3::Error ComposerResources::addVirtualDisplay(
    int64_t displayId, uint32_t outputBufferCacheSize) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  return toHwc3Error(mImpl->addVirtualDisplay(display, outputBufferCacheSize));
}

HWC3::Error ComposerResources::removeDisplay(int64_t displayId) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  return toHwc3Error(mImpl->removeDisplay(display));
}

HWC3::Error ComposerResources::setDisplayClientTargetCacheSize(
    int64_t displayId, uint32_t clientTargetCacheSize) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  return toHwc3Error(
      mImpl->setDisplayClientTargetCacheSize(display, clientTargetCacheSize));
}

HWC3::Error ComposerResources::getDisplayClientTargetCacheSize(
    int64_t displayId, size_t* outCacheSize) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  return toHwc3Error(
      mImpl->getDisplayClientTargetCacheSize(display, outCacheSize));
}

HWC3::Error ComposerResources::getDisplayOutputBufferCacheSize(
    int64_t displayId, size_t* outCacheSize) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  return toHwc3Error(
      mImpl->getDisplayOutputBufferCacheSize(display, outCacheSize));
}

HWC3::Error ComposerResources::addLayer(int64_t displayId, int64_t layerId,
                                        uint32_t bufferCacheSize) {
  DEBUG_LOG("%s: display:%" PRId64 " layer:%" PRId64, __FUNCTION__, displayId,
            layerId);

  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  ::android::hardware::graphics::composer::V2_1::Layer layer =
      toHwc2Layer(layerId);
  return toHwc3Error(mImpl->addLayer(display, layer, bufferCacheSize));
}

HWC3::Error ComposerResources::removeLayer(int64_t displayId, int64_t layerId) {
  DEBUG_LOG("%s: display:%" PRId64 " layer:%" PRId64, __FUNCTION__, displayId,
            layerId);

  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  ::android::hardware::graphics::composer::V2_1::Layer layer =
      toHwc2Layer(layerId);

  return toHwc3Error(mImpl->removeLayer(display, layer));
}

void ComposerResources::setDisplayMustValidateState(int64_t displayId,
                                                    bool mustValidate) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  mImpl->setDisplayMustValidateState(display, mustValidate);
}

bool ComposerResources::mustValidateDisplay(int64_t displayId) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  return mImpl->mustValidateDisplay(display);
}

HWC3::Error ComposerResources::getDisplayReadbackBuffer(
    int64_t displayId,
    const aidl::android::hardware::common::NativeHandle& handle,
    buffer_handle_t* outHandle, ComposerResourceReleaser* releaser) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  return toHwc3Error(mImpl->getDisplayReadbackBuffer(
      display, ::android::makeFromAidl(handle), outHandle,
      releaser->getReplacedHandle()));
}

HWC3::Error ComposerResources::getDisplayClientTarget(
    int64_t displayId, const Buffer& buffer, buffer_handle_t* outHandle,
    ComposerResourceReleaser* releaser) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);

  const bool useCache = !buffer.handle.has_value();

  buffer_handle_t bufferHandle = nullptr;
  if (buffer.handle.has_value()) {
    bufferHandle = ::android::makeFromAidl(*buffer.handle);
  }

  return toHwc3Error(mImpl->getDisplayClientTarget(
      display, static_cast<uint32_t>(buffer.slot), useCache, bufferHandle,
      outHandle, releaser->getReplacedHandle()));
}

HWC3::Error ComposerResources::getDisplayOutputBuffer(
    int64_t displayId, const Buffer& buffer, buffer_handle_t* outHandle,
    ComposerResourceReleaser* releaser) {
  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);

  const bool useCache = !buffer.handle.has_value();

  buffer_handle_t bufferHandle = nullptr;
  if (buffer.handle.has_value()) {
    bufferHandle = ::android::makeFromAidl(*buffer.handle);
  }

  return toHwc3Error(mImpl->getDisplayOutputBuffer(
      display, static_cast<uint32_t>(buffer.slot), useCache, bufferHandle,
      outHandle, releaser->getReplacedHandle()));
}

HWC3::Error ComposerResources::getLayerBuffer(
    int64_t displayId, int64_t layerId, const Buffer& buffer,
    buffer_handle_t* outHandle, ComposerResourceReleaser* releaser) {
  DEBUG_LOG("%s: display:%" PRId64 " layer:%" PRId64, __FUNCTION__, displayId,
            layerId);

  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  ::android::hardware::graphics::composer::V2_1::Layer layer =
      toHwc2Layer(layerId);

  const bool useCache = !buffer.handle.has_value();

  buffer_handle_t bufferHandle = nullptr;
  if (buffer.handle.has_value()) {
    bufferHandle = ::android::makeFromAidl(*buffer.handle);
  }

  DEBUG_LOG("%s fromCache:%s", __FUNCTION__, (useCache ? "yes" : "no"));
  return toHwc3Error(mImpl->getLayerBuffer(
      display, layer, static_cast<uint32_t>(buffer.slot), useCache,
      bufferHandle, outHandle, releaser->getReplacedHandle()));
}

HWC3::Error ComposerResources::getLayerSidebandStream(
    int64_t displayId, int64_t layerId,
    const aidl::android::hardware::common::NativeHandle& handle,
    buffer_handle_t* outHandle, ComposerResourceReleaser* releaser) {
  DEBUG_LOG("%s: display:%" PRId64 " layer:%" PRId64, __FUNCTION__, displayId,
            layerId);

  ::android::hardware::graphics::composer::V2_1::Display display =
      toHwc2Display(displayId);
  ::android::hardware::graphics::composer::V2_1::Layer layer =
      toHwc2Layer(layerId);
  return toHwc3Error(mImpl->getLayerSidebandStream(
      display, layer, ::android::makeFromAidl(handle), outHandle,
      releaser->getReplacedHandle()));
}

}  // namespace aidl::android::hardware::graphics::composer3::impl