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

#include "Layer.h"

#include <android-base/unique_fd.h>
#include <sync/sync.h>

#include <atomic>
#include <cmath>

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

std::atomic<int64_t> sNextId{1};

}  // namespace

Layer::Layer() : mId(sNextId++) {}

HWC3::Error Layer::setCursorPosition(const common::Point& position) {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  if (mCompositionType != Composition::CURSOR) {
    ALOGE("%s: CompositionType not Cursor type", __FUNCTION__);
    return HWC3::Error::BadLayer;
  }

  mCursorPosition = position;
  return HWC3::Error::None;
}

common::Point Layer::getCursorPosition() const {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  return mCursorPosition;
}

HWC3::Error Layer::setBuffer(buffer_handle_t buffer,
                             const ndk::ScopedFileDescriptor& fence) {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  if (buffer == nullptr) {
    ALOGE("%s: missing handle", __FUNCTION__);
    return HWC3::Error::BadParameter;
  }

  mBuffer.set(buffer, fence);
  return HWC3::Error::None;
}

FencedBuffer& Layer::getBuffer() {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  return mBuffer;
}

buffer_handle_t Layer::waitAndGetBuffer() {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  ::android::base::unique_fd fence = mBuffer.getFence();
  if (fence.ok()) {
    int err = sync_wait(fence.get(), 3000);
    if (err < 0 && errno == ETIME) {
      ALOGE("%s waited on fence %" PRId32 " for 3000 ms", __FUNCTION__,
            fence.get());
    }
  }

  return mBuffer.getBuffer();
}

HWC3::Error Layer::setSurfaceDamage(
    const std::vector<std::optional<common::Rect>>& /*damage*/) {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  return HWC3::Error::None;
}

HWC3::Error Layer::setBlendMode(common::BlendMode blendMode) {
  const auto blendModeString = toString(blendMode);
  DEBUG_LOG("%s: layer:%" PRId64 " blend mode:%s", __FUNCTION__, mId,
            blendModeString.c_str());

  mBlendMode = blendMode;
  return HWC3::Error::None;
}

common::BlendMode Layer::getBlendMode() const {
  const auto blendMode = mBlendMode;
  const auto blendModeString = toString(blendMode);
  DEBUG_LOG("%s: layer:%" PRId64 " blend mode:%s", __FUNCTION__, mId,
            blendModeString.c_str());

  return blendMode;
}

HWC3::Error Layer::setColor(Color color) {
  DEBUG_LOG("%s: layer:%" PRId64
            " color-r:%f color-g:%f color-b:%f color-a:%f)",
            __FUNCTION__, mId, color.r, color.g, color.b, color.a);

  mColor = color;
  return HWC3::Error::None;
}

Color Layer::getColor() const {
  auto color = mColor;
  DEBUG_LOG("%s: layer:%" PRId64
            " color-r:%f color-g:%f color-b:%f color-a:%f)",
            __FUNCTION__, mId, color.r, color.g, color.b, color.a);

  return color;
}

HWC3::Error Layer::setCompositionType(Composition compositionType) {
  const auto compositionTypeString = toString(compositionType);
  DEBUG_LOG("%s: layer:%" PRId64 " composition type:%s", __FUNCTION__, mId,
            compositionTypeString.c_str());

  mCompositionType = compositionType;
  return HWC3::Error::None;
}

Composition Layer::getCompositionType() const {
  const auto compositionTypeString = toString(mCompositionType);
  DEBUG_LOG("%s: layer:%" PRId64 " composition type:%s", __FUNCTION__, mId,
            compositionTypeString.c_str());

  return mCompositionType;
}

HWC3::Error Layer::setDataspace(common::Dataspace dataspace) {
  const auto dataspaceString = toString(dataspace);
  DEBUG_LOG("%s: layer:%" PRId64 " dataspace:%s", __FUNCTION__, mId,
            dataspaceString.c_str());

  mDataspace = dataspace;
  return HWC3::Error::None;
}

common::Dataspace Layer::getDataspace() const {
  const auto dataspaceString = toString(mDataspace);
  DEBUG_LOG("%s: layer:%" PRId64 " dataspace:%s", __FUNCTION__, mId,
            dataspaceString.c_str());

  return mDataspace;
}

HWC3::Error Layer::setDisplayFrame(common::Rect frame) {
  DEBUG_LOG("%s: layer:%" PRId64
            " display frame rect-left:%d rect-top:%d rect-right:%d rect-bot:%d",
            __FUNCTION__, mId, frame.left, frame.top, frame.right,
            frame.bottom);

  mDisplayFrame = frame;
  return HWC3::Error::None;
}

common::Rect Layer::getDisplayFrame() const {
  auto frame = mDisplayFrame;
  DEBUG_LOG("%s: layer:%" PRId64
            " display frame rect-left:%d rect-top:%d rect-right:%d rect-bot:%d",
            __FUNCTION__, mId, frame.left, frame.top, frame.right,
            frame.bottom);

  return frame;
}

HWC3::Error Layer::setPlaneAlpha(float alpha) {
  DEBUG_LOG("%s: layer:%" PRId64 "alpha:%f", __FUNCTION__, mId, alpha);

  mPlaneAlpha = alpha;
  return HWC3::Error::None;
}

float Layer::getPlaneAlpha() const {
  auto alpha = mPlaneAlpha;
  DEBUG_LOG("%s: layer:%" PRId64 "alpha:%f", __FUNCTION__, mId, alpha);

  return alpha;
}

HWC3::Error Layer::setSidebandStream(buffer_handle_t /*stream*/) {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  return HWC3::Error::None;
}

HWC3::Error Layer::setSourceCrop(common::FRect crop) {
  DEBUG_LOG("%s: layer:%" PRId64
            "crop rect-left:%f rect-top:%f rect-right:%f rect-bot:%f",
            __FUNCTION__, mId, crop.left, crop.top, crop.right, crop.bottom);

  mSourceCrop = crop;
  return HWC3::Error::None;
}

common::FRect Layer::getSourceCrop() const {
  common::FRect crop = mSourceCrop;
  DEBUG_LOG("%s: layer:%" PRId64
            "crop rect-left:%f rect-top:%f rect-right:%f rect-bot:%f",
            __FUNCTION__, mId, crop.left, crop.top, crop.right, crop.bottom);

  return crop;
}

common::Rect Layer::getSourceCropInt() const {
  common::Rect crop = {};
  crop.left = static_cast<int>(mSourceCrop.left);
  crop.top = static_cast<int>(mSourceCrop.top);
  crop.right = static_cast<int>(mSourceCrop.right);
  crop.bottom = static_cast<int>(mSourceCrop.bottom);
  DEBUG_LOG("%s: layer:%" PRId64
            "crop rect-left:%d rect-top:%d rect-right:%d rect-bot:%d",
            __FUNCTION__, mId, crop.left, crop.top, crop.right, crop.bottom);

  return crop;
}

HWC3::Error Layer::setTransform(common::Transform transform) {
  const auto transformString = toString(transform);
  DEBUG_LOG("%s: layer:%" PRId64 " transform:%s", __FUNCTION__, mId,
            transformString.c_str());

  mTransform = transform;
  return HWC3::Error::None;
}

common::Transform Layer::getTransform() const {
  const auto transformString = toString(mTransform);
  DEBUG_LOG("%s: layer:%" PRId64 " transform:%s", __FUNCTION__, mId,
            transformString.c_str());

  return mTransform;
}

HWC3::Error Layer::setVisibleRegion(
    const std::vector<std::optional<common::Rect>>& visible) {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  mVisibleRegion.clear();
  mVisibleRegion.reserve(visible.size());
  for (const auto& rectOption : visible) {
    if (rectOption) {
      mVisibleRegion.push_back(*rectOption);
    }
  }

  return HWC3::Error::None;
}

std::size_t Layer::getNumVisibleRegions() const {
  const std::size_t num = mVisibleRegion.size();
  DEBUG_LOG("%s: layer:%" PRId64 " number of visible regions: %zu",
            __FUNCTION__, mId, num);

  return num;
}

HWC3::Error Layer::setZOrder(int32_t z) {
  DEBUG_LOG("%s: layer:%" PRId64 " z:%d", __FUNCTION__, mId, z);

  mZOrder = z;
  return HWC3::Error::None;
}

int32_t Layer::getZOrder() const {
  DEBUG_LOG("%s: layer:%" PRId64 " z:%d", __FUNCTION__, mId, mZOrder);

  return mZOrder;
}

HWC3::Error Layer::setPerFrameMetadata(
    const std::vector<std::optional<PerFrameMetadata>>& /*perFrameMetadata*/) {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  return HWC3::Error::None;
}

HWC3::Error Layer::setColorTransform(const std::vector<float>& colorTransform) {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  if (colorTransform.size() < 16) {
    return HWC3::Error::BadParameter;
  }

  mColorTransform.emplace();
  std::copy_n(colorTransform.data(), 16, mColorTransform->data());
  return HWC3::Error::None;
}

const std::optional<std::array<float, 16>>& Layer::getColorTransform() const {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  return mColorTransform;
}

HWC3::Error Layer::setBrightness(float brightness) {
  DEBUG_LOG("%s: layer:%" PRId64 " brightness:%f", __FUNCTION__, mId,
            brightness);

  if (std::isnan(brightness) || brightness < 0.0f || brightness > 1.0f) {
    ALOGE("%s: layer:%" PRId64 " brightness:%f", __FUNCTION__, mId, brightness);
    return HWC3::Error::BadParameter;
  }

  mBrightness = brightness;
  return HWC3::Error::None;
}

float Layer::getBrightness() const {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  return mBrightness;
}

HWC3::Error Layer::setPerFrameMetadataBlobs(
    const std::vector<
        std::optional<PerFrameMetadataBlob>>& /*perFrameMetadata*/) {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  return HWC3::Error::None;
}

HWC3::Error Layer::setLuts(const Luts& luts) {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  mHasLuts = luts.pfd.get() >= 0;
  return HWC3::Error::None;
}

bool Layer::hasLuts() const {
  DEBUG_LOG("%s: layer:%" PRId64, __FUNCTION__, mId);

  return mHasLuts;
}

void Layer::logCompositionFallbackIfChanged(Composition to) {
  Composition from = getCompositionType();
  if (mLastCompositionFallback && mLastCompositionFallback->from == from &&
      mLastCompositionFallback->to == to) {
    return;
  }
  ALOGI("%s: layer %" PRIu32 " CompositionType fallback from %d to %d",
        __FUNCTION__, static_cast<uint32_t>(getId()), static_cast<int>(from),
        static_cast<int>(to));
  mLastCompositionFallback = {
      .from = from,
      .to = to,
  };
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
