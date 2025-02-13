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

#ifndef ANDROID_HWC_LAYER_H
#define ANDROID_HWC_LAYER_H

#include <optional>
#include <vector>

#include "Common.h"
#include "FencedBuffer.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class Layer {
 public:
  explicit Layer();

  Layer(const Layer&) = delete;
  Layer& operator=(const Layer&) = delete;

  Layer(Layer&&) = delete;
  Layer& operator=(Layer&&) = delete;

  int64_t getId() const { return mId; }

  HWC3::Error setCursorPosition(const common::Point& cursorPosition);
  common::Point getCursorPosition() const;

  HWC3::Error setBuffer(buffer_handle_t buffer,
                        const ndk::ScopedFileDescriptor& fence);
  FencedBuffer& getBuffer();
  buffer_handle_t waitAndGetBuffer();

  HWC3::Error setSurfaceDamage(
      const std::vector<std::optional<common::Rect>>& damage);

  HWC3::Error setBlendMode(common::BlendMode mode);
  common::BlendMode getBlendMode() const;

  HWC3::Error setColor(Color color);
  Color getColor() const;

  HWC3::Error setCompositionType(Composition composition);
  Composition getCompositionType() const;

  HWC3::Error setDataspace(common::Dataspace dataspace);
  common::Dataspace getDataspace() const;

  HWC3::Error setDisplayFrame(common::Rect frame);
  common::Rect getDisplayFrame() const;

  HWC3::Error setPlaneAlpha(float alpha);
  float getPlaneAlpha() const;

  HWC3::Error setSidebandStream(buffer_handle_t stream);

  HWC3::Error setSourceCrop(common::FRect crop);
  common::FRect getSourceCrop() const;
  common::Rect getSourceCropInt() const;

  HWC3::Error setTransform(common::Transform transform);
  common::Transform getTransform() const;

  HWC3::Error setVisibleRegion(
      const std::vector<std::optional<common::Rect>>& visible);
  std::size_t getNumVisibleRegions() const;

  HWC3::Error setZOrder(int32_t z);
  int32_t getZOrder() const;

  HWC3::Error setPerFrameMetadata(
      const std::vector<std::optional<PerFrameMetadata>>& perFrameMetadata);

  HWC3::Error setColorTransform(const std::vector<float>& colorTransform);
  const std::optional<std::array<float, 16>>& getColorTransform() const;

  HWC3::Error setBrightness(float brightness);
  float getBrightness() const;

  HWC3::Error setPerFrameMetadataBlobs(
      const std::vector<std::optional<PerFrameMetadataBlob>>& perFrameMetadata);

  HWC3::Error setLuts(const Luts& luts);
  bool hasLuts() const;

  // For log use only.
  void logCompositionFallbackIfChanged(Composition to);

 private:
  const int64_t mId;
  common::Point mCursorPosition;
  FencedBuffer mBuffer;
  common::BlendMode mBlendMode = common::BlendMode::NONE;
  Color mColor = {0, 0, 0, 0};
  Composition mCompositionType = Composition::INVALID;
  common::Dataspace mDataspace = common::Dataspace::UNKNOWN;
  struct CompositionTypeFallback {
    Composition from;
    Composition to;
  };
  // For log use only.
  std::optional<CompositionTypeFallback> mLastCompositionFallback =
      std::nullopt;
  common::Rect mDisplayFrame = {0, 0, -1, -1};
  float mPlaneAlpha = 0.0f;
  common::FRect mSourceCrop = {0.0f, 0.0f, -1.0f, -1.0f};
  common::Transform mTransform = common::Transform{0};
  std::vector<common::Rect> mVisibleRegion;
  int32_t mZOrder = 0;
  std::optional<std::array<float, 16>> mColorTransform;
  float mBrightness = 1.0f;
  bool mHasLuts = false;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
