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

#include "GuestFrameComposer.h"

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
#include "DisplayFinder.h"
#include "Drm.h"
#include "Layer.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

// Returns a color matrix that can be used with libyuv by converting values
// in -1 to 1 into -64 to 64 and converting row-major to column-major by
// transposing.
std::array<std::int8_t, 16> ToLibyuvColorMatrix(
    const std::array<float, 16>& in) {
  std::array<std::int8_t, 16> out;

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int indexIn = (4 * r) + c;
      int indexOut = (4 * c) + r;

      float clampedValue = std::max(
          -128.0f,
          std::min(127.0f, in[static_cast<size_t>(indexIn)] * 64.0f + 0.5f));
      out[(size_t)indexOut] = static_cast<std::int8_t>(clampedValue);
    }
  }

  return out;
}

std::uint8_t ToLibyuvColorChannel(float v) {
  return static_cast<std::uint8_t>(
      std::min(255, static_cast<int>(v * 255.0f + 0.5f)));
}

std::uint32_t ToLibyuvColor(float r, float g, float b, float a) {
  std::uint32_t out;
  std::uint8_t* outChannels = reinterpret_cast<std::uint8_t*>(&out);
  outChannels[0] = ToLibyuvColorChannel(r);
  outChannels[1] = ToLibyuvColorChannel(g);
  outChannels[2] = ToLibyuvColorChannel(b);
  outChannels[3] = ToLibyuvColorChannel(a);
  return out;
}

using ::android::hardware::graphics::common::V1_0::ColorTransform;

uint32_t AlignToPower2(uint32_t val, uint8_t align_log) {
  uint32_t align = 1 << align_log;
  return ((val + (align - 1)) / align) * align;
}

bool LayerNeedsScaling(const Layer& layer) {
  common::Rect crop = layer.getSourceCropInt();
  common::Rect frame = layer.getDisplayFrame();

  int fromW = crop.right - crop.left;
  int fromH = crop.bottom - crop.top;
  int toW = frame.right - frame.left;
  int toH = frame.bottom - frame.top;

  bool not_rot_scale = fromW != toW || fromH != toH;
  bool rot_scale = fromW != toH || fromH != toW;

  bool needs_rot = static_cast<int32_t>(layer.getTransform()) &
                   static_cast<int32_t>(common::Transform::ROT_90);

  return needs_rot ? rot_scale : not_rot_scale;
}

bool LayerNeedsBlending(const Layer& layer) {
  return layer.getBlendMode() != common::BlendMode::NONE;
}

bool LayerNeedsAttenuation(const Layer& layer) {
  return layer.getBlendMode() == common::BlendMode::COVERAGE;
}

struct BufferSpec;
typedef int (*ConverterFunction)(const BufferSpec& src, const BufferSpec& dst,
                                 bool v_flip);
int DoCopy(const BufferSpec& src, const BufferSpec& dst, bool vFlip);
int ConvertFromRGB565(const BufferSpec& src, const BufferSpec& dst, bool vFlip);
int ConvertFromYV12(const BufferSpec& src, const BufferSpec& dst, bool vFlip);

ConverterFunction GetConverterForDrmFormat(uint32_t drmFormat) {
  switch (drmFormat) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
      return &DoCopy;
    case DRM_FORMAT_RGB565:
      return &ConvertFromRGB565;
    case DRM_FORMAT_YVU420:
      return &ConvertFromYV12;
  }
  DEBUG_LOG("Unsupported drm format: %d(%s), returning null converter",
            drmFormat, GetDrmFormatString(drmFormat));
  return nullptr;
}

bool IsDrmFormatSupported(uint32_t drmFormat) {
  return GetConverterForDrmFormat(drmFormat) != nullptr;
}

// Libyuv's convert functions only allow the combination of any rotation
// (multiple of 90 degrees) and a vertical flip, but not horizontal flips.
// Surfaceflinger's transformations are expressed in terms of a vertical flip,
// a horizontal flip and/or a single 90 degrees clockwise rotation (see
// NATIVE_WINDOW_TRANSFORM_HINT documentation on system/window.h for more
// insight). The following code allows to turn a horizontal flip into a 180
// degrees rotation and a vertical flip.
libyuv::RotationMode GetRotationFromTransform(common::Transform transform) {
  uint32_t rotation = 0;
  rotation += (static_cast<int32_t>(transform) &
               static_cast<int32_t>(common::Transform::ROT_90))
                  ? 1
                  : 0;  // 1 * ROT90 bit
  rotation += (static_cast<int32_t>(transform) &
               static_cast<int32_t>(common::Transform::FLIP_H))
                  ? 2
                  : 0;  // 2 * VFLIP bit
  return static_cast<libyuv::RotationMode>(90 * rotation);
}

bool GetVFlipFromTransform(common::Transform transform) {
  // vertical flip xor horizontal flip
  bool hasVFlip = static_cast<int32_t>(transform) &
                  static_cast<int32_t>(common::Transform::FLIP_V);
  bool hasHFlip = static_cast<int32_t>(transform) &
                  static_cast<int32_t>(common::Transform::FLIP_H);
  return hasVFlip ^ hasHFlip;
}

struct BufferSpec {
  uint8_t* buffer;
  std::optional<android_ycbcr> buffer_ycbcr;
  uint32_t width;
  uint32_t height;
  uint32_t cropX;
  uint32_t cropY;
  uint32_t cropWidth;
  uint32_t cropHeight;
  uint32_t drmFormat;
  uint32_t strideBytes;
  uint32_t sampleBytes;

  BufferSpec() = default;

  BufferSpec(uint8_t* buffer, std::optional<android_ycbcr> buffer_ycbcr,
             uint32_t width, uint32_t height, uint32_t cropX, uint32_t cropY,
             uint32_t cropWidth, uint32_t cropHeight, uint32_t drmFormat,
             uint32_t strideBytes, uint32_t sampleBytes)
      : buffer(buffer),
        buffer_ycbcr(buffer_ycbcr),
        width(width),
        height(height),
        cropX(cropX),
        cropY(cropY),
        cropWidth(cropWidth),
        cropHeight(cropHeight),
        drmFormat(drmFormat),
        strideBytes(strideBytes),
        sampleBytes(sampleBytes) {}

  BufferSpec(uint8_t* buffer, uint32_t width, uint32_t height,
             uint32_t strideBytes)
      : BufferSpec(buffer,
                   /*buffer_ycbcr=*/std::nullopt, width, height,
                   /*cropX=*/0,
                   /*cropY=*/0,
                   /*cropWidth=*/width,
                   /*cropHeight=*/height,
                   /*drmFormat=*/DRM_FORMAT_ABGR8888, strideBytes,
                   /*sampleBytes=*/4) {}
};

int DoFill(const BufferSpec& dst, const Color& color) {
  ATRACE_CALL();

  const uint8_t r = static_cast<uint8_t>(color.r * 255.0f);
  const uint8_t g = static_cast<uint8_t>(color.g * 255.0f);
  const uint8_t b = static_cast<uint8_t>(color.b * 255.0f);
  const uint8_t a = static_cast<uint8_t>(color.a * 255.0f);

  const uint32_t rgba =
      static_cast<uint32_t>(r) | static_cast<uint32_t>(g) << 8 |
      static_cast<uint32_t>(b) << 16 | static_cast<uint32_t>(a) << 24;

  // Point to the upper left corner of the crop rectangle.
  uint8_t* dstBuffer =
      dst.buffer + dst.cropY * dst.strideBytes + dst.cropX * dst.sampleBytes;

  libyuv::SetPlane(dstBuffer, static_cast<int>(dst.strideBytes),
                   static_cast<int>(dst.cropWidth),
                   static_cast<int>(dst.cropHeight), rgba);
  return 0;
}

int ConvertFromRGB565(const BufferSpec& src, const BufferSpec& dst,
                      bool vFlip) {
  ATRACE_CALL();

  // Point to the upper left corner of the crop rectangle
  uint8_t* srcBuffer =
      src.buffer + src.cropY * src.strideBytes + src.cropX * src.sampleBytes;
  const int srcStrideBytes = static_cast<int>(src.strideBytes);
  uint8_t* dstBuffer =
      dst.buffer + dst.cropY * dst.strideBytes + dst.cropX * dst.sampleBytes;
  const int dstStrideBytes = static_cast<int>(dst.strideBytes);

  int width = static_cast<int>(src.cropWidth);
  int height = static_cast<int>(src.cropHeight);
  if (vFlip) {
    height = -height;
  }

  return libyuv::RGB565ToARGB(srcBuffer, srcStrideBytes,  //
                              dstBuffer, dstStrideBytes,  //
                              width, height);
}

int ConvertFromYV12(const BufferSpec& src, const BufferSpec& dst, bool vFlip) {
  ATRACE_CALL();

  // The following calculation of plane offsets and alignments are based on
  // swiftshader's Sampler::setTextureLevel() implementation
  // (Renderer/Sampler.cpp:225)

  auto& srcBufferYCbCrOpt = src.buffer_ycbcr;
  if (!srcBufferYCbCrOpt) {
    ALOGE("%s called on non ycbcr buffer", __FUNCTION__);
    return -1;
  }
  auto& srcBufferYCbCr = *srcBufferYCbCrOpt;

  // The libyuv::I420ToARGB() function is for tri-planar.
  if (srcBufferYCbCr.chroma_step != 1) {
    ALOGE("%s called with bad chroma step", __FUNCTION__);
    return -1;
  }

  uint8_t* srcY = reinterpret_cast<uint8_t*>(srcBufferYCbCr.y);
  const int strideYBytes = static_cast<int>(srcBufferYCbCr.ystride);
  uint8_t* srcU = reinterpret_cast<uint8_t*>(srcBufferYCbCr.cb);
  const int strideUBytes = static_cast<int>(srcBufferYCbCr.cstride);
  uint8_t* srcV = reinterpret_cast<uint8_t*>(srcBufferYCbCr.cr);
  const int strideVBytes = static_cast<int>(srcBufferYCbCr.cstride);

  // Adjust for crop
  srcY += src.cropY * srcBufferYCbCr.ystride + src.cropX;
  srcV += (src.cropY / 2) * srcBufferYCbCr.cstride + (src.cropX / 2);
  srcU += (src.cropY / 2) * srcBufferYCbCr.cstride + (src.cropX / 2);
  uint8_t* dstBuffer =
      dst.buffer + dst.cropY * dst.strideBytes + dst.cropX * dst.sampleBytes;
  const int dstStrideBytes = static_cast<int>(dst.strideBytes);

  int width = static_cast<int>(dst.cropWidth);
  int height = static_cast<int>(dst.cropHeight);

  if (vFlip) {
    height = -height;
  }

  // YV12 is the same as I420, with the U and V planes swapped
  return libyuv::I420ToARGB(srcY, strideYBytes,  //
                            srcV, strideVBytes,  //
                            srcU, strideUBytes,  //
                            dstBuffer, dstStrideBytes, width, height);
}

int DoConversion(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  ConverterFunction func = GetConverterForDrmFormat(src.drmFormat);
  if (!func) {
    // GetConverterForDrmFormat should've logged the issue for us.
    return -1;
  }
  return func(src, dst, v_flip);
}

int DoCopy(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  ATRACE_CALL();

  // Point to the upper left corner of the crop rectangle
  uint8_t* srcBuffer =
      src.buffer + src.cropY * src.strideBytes + src.cropX * src.sampleBytes;
  const int srcStrideBytes = static_cast<int>(src.strideBytes);
  uint8_t* dstBuffer =
      dst.buffer + dst.cropY * dst.strideBytes + dst.cropX * dst.sampleBytes;
  const int dstStrideBytes = static_cast<int>(dst.strideBytes);
  int width = static_cast<int>(src.cropWidth);
  int height = static_cast<int>(src.cropHeight);

  if (v_flip) {
    height = -height;
  }

  // HAL formats are named based on the order of the pixel components on the
  // byte stream, while libyuv formats are named based on the order of those
  // pixel components in an integer written from left to right. So
  // libyuv::FOURCC_ARGB is equivalent to HAL_PIXEL_FORMAT_BGRA_8888.
  auto ret = libyuv::ARGBCopy(srcBuffer, srcStrideBytes,  //
                              dstBuffer, dstStrideBytes,  //
                              width, height);
  return ret;
}

int DoRotation(const BufferSpec& src, const BufferSpec& dst,
               libyuv::RotationMode rotation, bool v_flip) {
  ATRACE_CALL();

  // Point to the upper left corner of the crop rectangles
  uint8_t* srcBuffer =
      src.buffer + src.cropY * src.strideBytes + src.cropX * src.sampleBytes;
  const int srcStrideBytes = static_cast<int>(src.strideBytes);
  uint8_t* dstBuffer =
      dst.buffer + dst.cropY * dst.strideBytes + dst.cropX * dst.sampleBytes;
  const int dstStrideBytes = static_cast<int>(dst.strideBytes);
  int width = static_cast<int>(src.cropWidth);
  int height = static_cast<int>(src.cropHeight);

  if (v_flip) {
    height = -height;
  }

  return libyuv::ARGBRotate(srcBuffer, srcStrideBytes,  //
                            dstBuffer, dstStrideBytes,  //
                            width, height, rotation);
}

int DoScaling(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  ATRACE_CALL();

  // Point to the upper left corner of the crop rectangles
  uint8_t* srcBuffer =
      src.buffer + src.cropY * src.strideBytes + src.cropX * src.sampleBytes;
  uint8_t* dstBuffer =
      dst.buffer + dst.cropY * dst.strideBytes + dst.cropX * dst.sampleBytes;
  const int srcStrideBytes = static_cast<int>(src.strideBytes);
  const int dstStrideBytes = static_cast<int>(dst.strideBytes);
  const int srcWidth = static_cast<int>(src.cropWidth);
  int srcHeight = static_cast<int>(src.cropHeight);
  const int dstWidth = static_cast<int>(dst.cropWidth);
  const int dstHeight = static_cast<int>(dst.cropHeight);

  if (v_flip) {
    srcHeight = -srcHeight;
  }

  return libyuv::ARGBScale(srcBuffer, srcStrideBytes, srcWidth, srcHeight,
                           dstBuffer, dstStrideBytes, dstWidth, dstHeight,
                           libyuv::kFilterBilinear);
}

int DoAttenuation(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  ATRACE_CALL();

  // Point to the upper left corner of the crop rectangles
  uint8_t* srcBuffer =
      src.buffer + src.cropY * src.strideBytes + src.cropX * src.sampleBytes;
  uint8_t* dstBuffer =
      dst.buffer + dst.cropY * dst.strideBytes + dst.cropX * dst.sampleBytes;
  const int srcStrideBytes = static_cast<int>(src.strideBytes);
  const int dstStrideBytes = static_cast<int>(dst.strideBytes);
  const int width = static_cast<int>(dst.cropWidth);
  int height = static_cast<int>(dst.cropHeight);
  if (v_flip) {
    height = -height;
  }

  return libyuv::ARGBAttenuate(srcBuffer, srcStrideBytes,  //
                               dstBuffer, dstStrideBytes,  //
                               width, height);
}

int DoBrightnessShading(const BufferSpec& src, const BufferSpec& dst,
                        float layerBrightness) {
  ATRACE_CALL();

  const float layerBrightnessGammaCorrected =
      std::pow(layerBrightness, 1.0f / 2.2f);

  const std::uint32_t shade = ToLibyuvColor(
      layerBrightnessGammaCorrected, layerBrightnessGammaCorrected,
      layerBrightnessGammaCorrected, 1.0f);

  auto ret = libyuv::ARGBShade(src.buffer, static_cast<int>(src.strideBytes),
                               dst.buffer, static_cast<int>(dst.strideBytes),
                               static_cast<int>(dst.width),
                               static_cast<int>(dst.height), shade);

  return ret;
}

int DoBlending(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  ATRACE_CALL();

  // Point to the upper left corner of the crop rectangles
  uint8_t* srcBuffer =
      src.buffer + src.cropY * src.strideBytes + src.cropX * src.sampleBytes;
  uint8_t* dstBuffer =
      dst.buffer + dst.cropY * dst.strideBytes + dst.cropX * dst.sampleBytes;
  const int srcStrideBytes = static_cast<int>(src.strideBytes);
  const int dstStrideBytes = static_cast<int>(dst.strideBytes);
  const int width = static_cast<int>(dst.cropWidth);
  int height = static_cast<int>(dst.cropHeight);
  if (v_flip) {
    height = -height;
  }

  // libyuv's ARGB format is hwcomposer's BGRA format, since blending only cares
  // for the position of alpha in the pixel and not the position of the colors
  // this function is perfectly usable.
  return libyuv::ARGBBlend(srcBuffer, srcStrideBytes,  //
                           dstBuffer, dstStrideBytes,  //
                           dstBuffer, dstStrideBytes,  //
                           width, height);
}

std::optional<BufferSpec> GetBufferSpec(GrallocBuffer& buffer,
                                        GrallocBufferView& bufferView,
                                        const common::Rect& bufferCrop) {
  auto bufferFormatOpt = buffer.GetDrmFormat();
  if (!bufferFormatOpt) {
    ALOGE("Failed to get gralloc buffer format.");
    return std::nullopt;
  }
  uint32_t bufferFormat = *bufferFormatOpt;

  auto bufferWidthOpt = buffer.GetWidth();
  if (!bufferWidthOpt) {
    ALOGE("Failed to get gralloc buffer width.");
    return std::nullopt;
  }
  uint32_t bufferWidth = *bufferWidthOpt;

  auto bufferHeightOpt = buffer.GetHeight();
  if (!bufferHeightOpt) {
    ALOGE("Failed to get gralloc buffer height.");
    return std::nullopt;
  }
  uint32_t bufferHeight = *bufferHeightOpt;

  uint8_t* bufferData = nullptr;
  uint32_t bufferStrideBytes = 0;
  std::optional<android_ycbcr> bufferYCbCrData;

  if (bufferFormat == DRM_FORMAT_NV12 || bufferFormat == DRM_FORMAT_NV21 ||
      bufferFormat == DRM_FORMAT_YVU420) {
    bufferYCbCrData = bufferView.GetYCbCr();
    if (!bufferYCbCrData) {
      ALOGE("%s failed to get raw ycbcr from view.", __FUNCTION__);
      return std::nullopt;
    }
  } else {
    auto bufferDataOpt = bufferView.Get();
    if (!bufferDataOpt) {
      ALOGE("%s failed to lock gralloc buffer.", __FUNCTION__);
      return std::nullopt;
    }
    bufferData = reinterpret_cast<uint8_t*>(*bufferDataOpt);

    auto bufferStrideBytesOpt = buffer.GetMonoPlanarStrideBytes();
    if (!bufferStrideBytesOpt) {
      ALOGE("%s failed to get plane stride.", __FUNCTION__);
      return std::nullopt;
    }
    bufferStrideBytes = *bufferStrideBytesOpt;
  }

  uint32_t bufferCropX = static_cast<uint32_t>(bufferCrop.left);
  uint32_t bufferCropY = static_cast<uint32_t>(bufferCrop.top);
  uint32_t bufferCropWidth =
      static_cast<uint32_t>(bufferCrop.right - bufferCrop.left);
  uint32_t bufferCropHeight =
      static_cast<uint32_t>(bufferCrop.bottom - bufferCrop.top);

  return BufferSpec(bufferData, bufferYCbCrData, bufferWidth, bufferHeight,
                    bufferCropX, bufferCropY, bufferCropWidth, bufferCropHeight,
                    bufferFormat, bufferStrideBytes,
                    GetDrmFormatBytesPerPixel(bufferFormat));
}

}  // namespace

HWC3::Error GuestFrameComposer::init() {
  DEBUG_LOG("%s", __FUNCTION__);

  HWC3::Error error = mDrmClient.init();
  if (error != HWC3::Error::None) {
    ALOGE("%s: failed to initialize DrmClient", __FUNCTION__);
    return error;
  }

  return HWC3::Error::None;
}

HWC3::Error GuestFrameComposer::registerOnHotplugCallback(
    const HotplugCallback& cb) {
  return mDrmClient.registerOnHotplugCallback(cb);
  return HWC3::Error::None;
}

HWC3::Error GuestFrameComposer::unregisterOnHotplugCallback() {
  return mDrmClient.unregisterOnHotplugCallback();
}

HWC3::Error GuestFrameComposer::onDisplayCreate(Display* display) {
  const uint32_t displayId = static_cast<uint32_t>(display->getId());
  int32_t displayConfigId;
  int32_t displayWidth;
  int32_t displayHeight;

  HWC3::Error error = display->getActiveConfig(&displayConfigId);
  if (error != HWC3::Error::None) {
    ALOGE("%s: display:%" PRIu32 " has no active config", __FUNCTION__,
          displayId);
    return error;
  }

  error = display->getDisplayAttribute(displayConfigId, DisplayAttribute::WIDTH,
                                       &displayWidth);
  if (error != HWC3::Error::None) {
    ALOGE("%s: display:%" PRIu32 " failed to get width", __FUNCTION__,
          displayId);
    return error;
  }

  error = display->getDisplayAttribute(
      displayConfigId, DisplayAttribute::HEIGHT, &displayHeight);
  if (error != HWC3::Error::None) {
    ALOGE("%s: display:%" PRIu32 " failed to get height", __FUNCTION__,
          displayId);
    return error;
  }

  auto it = mDisplayInfos.find(displayId);
  if (it != mDisplayInfos.end()) {
    ALOGE("%s: display:%" PRIu32 " already created?", __FUNCTION__, displayId);
  }

  DisplayInfo& displayInfo = mDisplayInfos[displayId];

  displayInfo.swapchain = DrmSwapchain::create(
      static_cast<uint32_t>(displayWidth), static_cast<uint32_t>(displayHeight),
      ::android::GraphicBuffer::USAGE_HW_COMPOSER |
          ::android::GraphicBuffer::USAGE_SW_READ_OFTEN |
          ::android::GraphicBuffer::USAGE_SW_WRITE_OFTEN,
      &mDrmClient);

  if (displayId == 0) {
    auto compositionResult = displayInfo.swapchain->getNextImage();
    auto [flushError, flushSyncFd] = mDrmClient.flushToDisplay(
        displayId, compositionResult->getDrmBuffer(), -1);
    if (flushError != HWC3::Error::None) {
      ALOGW(
          "%s: Initial display flush failed. HWComposer assuming that we are "
          "running in QEMU without a display and disabling presenting.",
          __FUNCTION__);
      mPresentDisabled = true;
    } else {
      compositionResult->markAsInUse(std::move(flushSyncFd));
    }
  }

  std::optional<std::vector<uint8_t>> edid = mDrmClient.getEdid(displayId);
  if (edid) {
    display->setEdid(*edid);
  }

  return HWC3::Error::None;
}

HWC3::Error GuestFrameComposer::onDisplayDestroy(Display* display) {
  auto displayId = display->getId();

  auto it = mDisplayInfos.find(displayId);
  if (it == mDisplayInfos.end()) {
    ALOGE("%s: display:%" PRIu64 " missing display buffers?", __FUNCTION__,
          displayId);
    return HWC3::Error::BadDisplay;
  }
  mDisplayInfos.erase(it);

  return HWC3::Error::None;
}

HWC3::Error GuestFrameComposer::onDisplayClientTargetSet(Display*) {
  return HWC3::Error::None;
}

HWC3::Error GuestFrameComposer::onActiveConfigChange(Display* /*display*/) {
  return HWC3::Error::None;
};

HWC3::Error GuestFrameComposer::getDisplayConfigsFromSystemProp(
    std::vector<GuestFrameComposer::DisplayConfig>* configs) {
  DEBUG_LOG("%s", __FUNCTION__);

  std::vector<int> propIntParts;
  parseExternalDisplaysFromProperties(propIntParts);

  while (!propIntParts.empty()) {
    DisplayConfig display_config = {
        .width = propIntParts[1],
        .height = propIntParts[2],
        .dpiX = propIntParts[3],
        .dpiY = propIntParts[3],
        .refreshRateHz = 160,
    };

    configs->push_back(display_config);

    propIntParts.erase(propIntParts.begin(), propIntParts.begin() + 5);
  }

  return HWC3::Error::None;
}

HWC3::Error GuestFrameComposer::validateDisplay(Display* display,
                                                DisplayChanges* outChanges) {
  const auto displayId = display->getId();
  DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

  const std::vector<Layer*>& layers = display->getOrderedLayers();

  bool fallbackToClientComposition = false;
  for (Layer* layer : layers) {
    const auto layerId = layer->getId();
    const auto layerCompositionType = layer->getCompositionType();
    const auto layerCompositionTypeString = toString(layerCompositionType);

    if (layerCompositionType == Composition::INVALID) {
      ALOGE("%s display:%" PRIu64 " layer:%" PRIu64 " has Invalid composition",
            __FUNCTION__, displayId, layerId);
      continue;
    }

    if (layerCompositionType == Composition::CLIENT ||
        layerCompositionType == Composition::CURSOR ||
        layerCompositionType == Composition::SIDEBAND) {
      DEBUG_LOG("%s: display:%" PRIu64 " layer:%" PRIu64
                " has composition type %s, falling back to client composition",
                __FUNCTION__, displayId, layerId,
                layerCompositionTypeString.c_str());
      fallbackToClientComposition = true;
      break;
    }

    if (layerCompositionType == Composition::DISPLAY_DECORATION) {
      return HWC3::Error::Unsupported;
    }

    if (!canComposeLayer(layer)) {
      DEBUG_LOG(
          "%s: display:%" PRIu64 " layer:%" PRIu64
          " composition not supported, falling back to client composition",
          __FUNCTION__, displayId, layerId);
      fallbackToClientComposition = true;
      break;
    }
  }

  if (fallbackToClientComposition) {
    for (Layer* layer : layers) {
      const auto layerId = layer->getId();
      const auto layerCompositionType = layer->getCompositionType();

      if (layerCompositionType == Composition::INVALID) {
        continue;
      }

      if (layerCompositionType != Composition::CLIENT) {
        DEBUG_LOG("%s display:%" PRIu64 " layer:%" PRIu64
                  "composition updated to Client",
                  __FUNCTION__, displayId, layerId);

        outChanges->addLayerCompositionChange(displayId, layerId,
                                              Composition::CLIENT);
      }
    }
  }

  // We can not draw below a Client (SurfaceFlinger) composed layer. Change all
  // layers below a Client composed layer to also be Client composed.
  if (layers.size() > 1) {
    for (std::size_t layerIndex = layers.size() - 1; layerIndex > 0;
         layerIndex--) {
      auto layer = layers[layerIndex];
      auto layerCompositionType = layer->getCompositionType();

      if (layerCompositionType == Composition::CLIENT) {
        for (std::size_t lowerLayerIndex = 0; lowerLayerIndex < layerIndex;
             lowerLayerIndex++) {
          auto lowerLayer = layers[lowerLayerIndex];
          auto lowerLayerId = lowerLayer->getId();
          auto lowerLayerCompositionType = lowerLayer->getCompositionType();

          if (lowerLayerCompositionType != Composition::CLIENT) {
            DEBUG_LOG("%s: display:%" PRIu64 " changing layer:%" PRIu64
                      " to Client because"
                      "hwcomposer can not draw below the Client composed "
                      "layer:%" PRIu64,
                      __FUNCTION__, displayId, lowerLayerId, layer->getId());

            outChanges->addLayerCompositionChange(displayId, lowerLayerId,
                                                  Composition::CLIENT);
          }
        }
      }
    }
  }

  return HWC3::Error::None;
}

HWC3::Error GuestFrameComposer::presentDisplay(
    Display* display, ::android::base::unique_fd* outDisplayFence,
    std::unordered_map<int64_t,
                       ::android::base::unique_fd>* /*outLayerFences*/) {
  const uint32_t displayId = static_cast<uint32_t>(display->getId());
  DEBUG_LOG("%s display:%" PRIu32, __FUNCTION__, displayId);

  if (mPresentDisabled) {
    return HWC3::Error::None;
  }

  auto it = mDisplayInfos.find(displayId);
  if (it == mDisplayInfos.end()) {
    ALOGE("%s: display:%" PRIu32 " not found", __FUNCTION__, displayId);
    return HWC3::Error::NoResources;
  }

  DisplayInfo& displayInfo = it->second;

  auto compositionResult = displayInfo.swapchain->getNextImage();
  compositionResult->wait();

  if (compositionResult->getBuffer() == nullptr) {
    ALOGE("%s: display:%" PRIu32 " missing composition result buffer",
          __FUNCTION__, displayId);
    return HWC3::Error::NoResources;
  }

  if (compositionResult->getDrmBuffer() == nullptr) {
    ALOGE("%s: display:%" PRIu32 " missing composition result drm buffer",
          __FUNCTION__, displayId);
    return HWC3::Error::NoResources;
  }

  std::optional<GrallocBuffer> compositionResultBufferOpt =
      mGralloc.Import(compositionResult->getBuffer());
  if (!compositionResultBufferOpt) {
    ALOGE("%s: display:%" PRIu32 " failed to import buffer", __FUNCTION__,
          displayId);
    return HWC3::Error::NoResources;
  }

  std::optional<uint32_t> compositionResultBufferWidthOpt =
      compositionResultBufferOpt->GetWidth();
  if (!compositionResultBufferWidthOpt) {
    ALOGE("%s: display:%" PRIu32 " failed to query buffer width", __FUNCTION__,
          displayId);
    return HWC3::Error::NoResources;
  }

  std::optional<uint32_t> compositionResultBufferHeightOpt =
      compositionResultBufferOpt->GetHeight();
  if (!compositionResultBufferHeightOpt) {
    ALOGE("%s: display:%" PRIu32 " failed to query buffer height", __FUNCTION__,
          displayId);
    return HWC3::Error::NoResources;
  }

  std::optional<uint32_t> compositionResultBufferStrideOpt =
      compositionResultBufferOpt->GetMonoPlanarStrideBytes();
  if (!compositionResultBufferStrideOpt) {
    ALOGE("%s: display:%" PRIu32 " failed to query buffer stride", __FUNCTION__,
          displayId);
    return HWC3::Error::NoResources;
  }

  std::optional<GrallocBufferView> compositionResultBufferViewOpt =
      compositionResultBufferOpt->Lock();
  if (!compositionResultBufferViewOpt) {
    ALOGE("%s: display:%" PRIu32 " failed to get buffer view", __FUNCTION__,
          displayId);
    return HWC3::Error::NoResources;
  }

  const std::optional<void*> compositionResultBufferDataOpt =
      compositionResultBufferViewOpt->Get();
  if (!compositionResultBufferDataOpt) {
    ALOGE("%s: display:%" PRIu32 " failed to get buffer data", __FUNCTION__,
          displayId);
    return HWC3::Error::NoResources;
  }

  uint32_t compositionResultBufferWidth = *compositionResultBufferWidthOpt;
  uint32_t compositionResultBufferHeight = *compositionResultBufferHeightOpt;
  uint32_t compositionResultBufferStride = *compositionResultBufferStrideOpt;
  uint8_t* compositionResultBufferData =
      reinterpret_cast<uint8_t*>(*compositionResultBufferDataOpt);

  const std::vector<Layer*>& layers = display->getOrderedLayers();

  const bool noOpComposition = layers.empty();
  const bool allLayersClientComposed =
      std::all_of(layers.begin(),  //
                  layers.end(),    //
                  [](const Layer* layer) {
                    return layer->getCompositionType() == Composition::CLIENT;
                  });

  if (noOpComposition) {
    ALOGW("%s: display:%" PRIu32 " empty composition", __FUNCTION__, displayId);
  } else if (allLayersClientComposed) {
    auto clientTargetBufferOpt =
        mGralloc.Import(display->waitAndGetClientTargetBuffer());
    if (!clientTargetBufferOpt) {
      ALOGE("%s: failed to import client target buffer.", __FUNCTION__);
      return HWC3::Error::NoResources;
    }
    GrallocBuffer& clientTargetBuffer = *clientTargetBufferOpt;

    auto clientTargetBufferViewOpt = clientTargetBuffer.Lock();
    if (!clientTargetBufferViewOpt) {
      ALOGE("%s: failed to lock client target buffer.", __FUNCTION__);
      return HWC3::Error::NoResources;
    }
    GrallocBufferView& clientTargetBufferView = *clientTargetBufferViewOpt;

    auto clientTargetPlaneLayoutsOpt = clientTargetBuffer.GetPlaneLayouts();
    if (!clientTargetPlaneLayoutsOpt) {
      ALOGE("Failed to get client target buffer plane layouts.");
      return HWC3::Error::NoResources;
    }
    auto& clientTargetPlaneLayouts = *clientTargetPlaneLayoutsOpt;

    if (clientTargetPlaneLayouts.size() != 1) {
      ALOGE("Unexpected number of plane layouts for client target buffer.");
      return HWC3::Error::NoResources;
    }

    std::size_t clientTargetPlaneSize =
        static_cast<std::size_t>(clientTargetPlaneLayouts[0].totalSizeInBytes);

    auto clientTargetDataOpt = clientTargetBufferView.Get();
    if (!clientTargetDataOpt) {
      ALOGE("%s failed to lock gralloc buffer.", __FUNCTION__);
      return HWC3::Error::NoResources;
    }
    auto* clientTargetData = reinterpret_cast<uint8_t*>(*clientTargetDataOpt);

    std::memcpy(compositionResultBufferData, clientTargetData,
                clientTargetPlaneSize);
  } else {
    for (Layer* layer : layers) {
      const auto layerId = layer->getId();
      const auto layerCompositionType = layer->getCompositionType();

      if (layerCompositionType != Composition::DEVICE &&
          layerCompositionType != Composition::SOLID_COLOR) {
        continue;
      }

      HWC3::Error error =
          composeLayerInto(displayInfo.compositionIntermediateStorage,  //
                           layer,                                       //
                           compositionResultBufferData,                 //
                           compositionResultBufferWidth,                //
                           compositionResultBufferHeight,               //
                           compositionResultBufferStride,               //
                           4);
      if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu32 " failed to compose layer:%" PRIu64,
              __FUNCTION__, displayId, layerId);
        return error;
      }
    }
  }

  if (display->hasColorTransform()) {
    HWC3::Error error =
        applyColorTransformToRGBA(display->getColorTransform(),   //
                                  compositionResultBufferData,    //
                                  compositionResultBufferWidth,   //
                                  compositionResultBufferHeight,  //
                                  compositionResultBufferStride);
    if (error != HWC3::Error::None) {
      ALOGE("%s: display:%" PRIu32 " failed to apply color transform",
            __FUNCTION__, displayId);
      return error;
    }
  }

  DEBUG_LOG("%s display:%" PRIu32 " flushing drm buffer", __FUNCTION__,
            displayId);

  auto [error, fence] = mDrmClient.flushToDisplay(
      displayId, compositionResult->getDrmBuffer(), -1);
  if (error != HWC3::Error::None) {
    ALOGE("%s: display:%" PRIu32 " failed to flush drm buffer" PRIu64,
          __FUNCTION__, displayId);
  }

  *outDisplayFence = std::move(fence);
  compositionResult->markAsInUse(
      outDisplayFence->ok() ? ::android::base::unique_fd(dup(*outDisplayFence))
                            : ::android::base::unique_fd());
  return error;
}

bool GuestFrameComposer::canComposeLayer(Layer* layer) {
  const auto layerCompositionType = layer->getCompositionType();
  if (layerCompositionType == Composition::SOLID_COLOR) {
    return true;
  }

  if (layerCompositionType != Composition::DEVICE) {
    return false;
  }

  buffer_handle_t bufferHandle = layer->getBuffer().getBuffer();
  if (bufferHandle == nullptr) {
    ALOGW("%s received a layer with a null handle", __FUNCTION__);
    return false;
  }

  auto bufferOpt = mGralloc.Import(bufferHandle);
  if (!bufferOpt) {
    ALOGE("Failed to import layer buffer.");
    return false;
  }
  GrallocBuffer& buffer = *bufferOpt;

  auto bufferFormatOpt = buffer.GetDrmFormat();
  if (!bufferFormatOpt) {
    ALOGE("Failed to get layer buffer format.");
    return false;
  }
  uint32_t bufferFormat = *bufferFormatOpt;

  if (!IsDrmFormatSupported(bufferFormat)) {
    return false;
  }

  if (layer->hasLuts()) {
    return false;
  }

  return true;
}

HWC3::Error GuestFrameComposer::composeLayerInto(
    AlternatingImageStorage& compositionIntermediateStorage,
    Layer* srcLayer,                     //
    std::uint8_t* dstBuffer,             //
    std::uint32_t dstBufferWidth,        //
    std::uint32_t dstBufferHeight,       //
    std::uint32_t dstBufferStrideBytes,  //
    std::uint32_t dstBufferBytesPerPixel) {
  ATRACE_CALL();

  libyuv::RotationMode rotation =
      GetRotationFromTransform(srcLayer->getTransform());

  common::Rect srcLayerCrop = srcLayer->getSourceCropInt();
  common::Rect srcLayerDisplayFrame = srcLayer->getDisplayFrame();

  BufferSpec srcLayerSpec;

  std::optional<GrallocBuffer> srcBufferOpt;
  std::optional<GrallocBufferView> srcBufferViewOpt;

  const auto srcLayerCompositionType = srcLayer->getCompositionType();
  if (srcLayerCompositionType == Composition::DEVICE) {
    srcBufferOpt = mGralloc.Import(srcLayer->waitAndGetBuffer());
    if (!srcBufferOpt) {
      ALOGE("%s: failed to import layer buffer.", __FUNCTION__);
      return HWC3::Error::NoResources;
    }
    GrallocBuffer& srcBuffer = *srcBufferOpt;

    srcBufferViewOpt = srcBuffer.Lock();
    if (!srcBufferViewOpt) {
      ALOGE("%s: failed to lock import layer buffer.", __FUNCTION__);
      return HWC3::Error::NoResources;
    }
    GrallocBufferView& srcBufferView = *srcBufferViewOpt;

    auto srcLayerSpecOpt =
        GetBufferSpec(srcBuffer, srcBufferView, srcLayerCrop);
    if (!srcLayerSpecOpt) {
      return HWC3::Error::NoResources;
    }

    srcLayerSpec = *srcLayerSpecOpt;
  } else if (srcLayerCompositionType == Composition::SOLID_COLOR) {
    // srcLayerSpec not used by `needsFill` below.
  }

  // TODO(jemoreira): Remove the hardcoded format.
  bool needsFill = srcLayerCompositionType == Composition::SOLID_COLOR;
  bool needsConversion = srcLayerCompositionType == Composition::DEVICE &&
                         srcLayerSpec.drmFormat != DRM_FORMAT_XBGR8888 &&
                         srcLayerSpec.drmFormat != DRM_FORMAT_ABGR8888;
  bool needsScaling = LayerNeedsScaling(*srcLayer);
  bool needsRotation = rotation != libyuv::kRotate0;
  bool needsTranspose = needsRotation && rotation != libyuv::kRotate180;
  bool needsVFlip = GetVFlipFromTransform(srcLayer->getTransform());
  bool needsAttenuation = LayerNeedsAttenuation(*srcLayer);
  bool needsBlending = LayerNeedsBlending(*srcLayer);
  bool needsBrightness = srcLayer->getBrightness() != 1.0f;
  bool needsCopy = !(needsConversion || needsScaling || needsRotation ||
                     needsVFlip || needsAttenuation || needsBlending);

  BufferSpec dstLayerSpec(
      dstBuffer,
      /*buffer_ycbcr=*/std::nullopt, dstBufferWidth, dstBufferHeight,
      static_cast<uint32_t>(srcLayerDisplayFrame.left),
      static_cast<uint32_t>(srcLayerDisplayFrame.top),
      static_cast<uint32_t>(srcLayerDisplayFrame.right -
                            srcLayerDisplayFrame.left),
      static_cast<uint32_t>(srcLayerDisplayFrame.bottom -
                            srcLayerDisplayFrame.top),
      DRM_FORMAT_XBGR8888, dstBufferStrideBytes, dstBufferBytesPerPixel);

  // Add the destination layer to the bottom of the buffer stack
  std::vector<BufferSpec> dstBufferStack(1, dstLayerSpec);

  // If more than operation is to be performed, a temporary buffer is needed for
  // each additional operation

  // N operations need N destination buffers, the destination layer (the
  // framebuffer) is one of them, so only N-1 temporary buffers are needed.
  // Vertical flip is not taken into account because it can be done together
  // with any other operation.
  int neededIntermediateImages =
      (needsFill ? 1 : 0) + (needsConversion ? 1 : 0) + (needsScaling ? 1 : 0) +
      (needsRotation ? 1 : 0) + (needsAttenuation ? 1 : 0) +
      (needsBlending ? 1 : 0) + (needsCopy ? 1 : 0) +
      (needsBrightness ? 1 : 0) - 1;

  uint32_t mScratchBufferWidth = static_cast<uint32_t>(
      srcLayerDisplayFrame.right - srcLayerDisplayFrame.left);
  uint32_t mScratchBufferHeight = static_cast<uint32_t>(
      srcLayerDisplayFrame.bottom - srcLayerDisplayFrame.top);
  uint32_t mScratchBufferStrideBytes =
      AlignToPower2(mScratchBufferWidth * dstBufferBytesPerPixel, 4);
  uint32_t mScratchBufferSizeBytes =
      mScratchBufferHeight * mScratchBufferStrideBytes;

  for (uint32_t i = 0; i < neededIntermediateImages; i++) {
    BufferSpec mScratchBufferspec(
        compositionIntermediateStorage.getRotatingScratchBuffer(
            mScratchBufferSizeBytes, i),
        mScratchBufferWidth, mScratchBufferHeight, mScratchBufferStrideBytes);
    dstBufferStack.push_back(mScratchBufferspec);
  }

  // Filling, conversion, and scaling should always be the first operations, so
  // that every other operation works on equally sized frames (guaranteed to fit
  // in the scratch buffers) in a common format.

  if (needsFill) {
    BufferSpec& dstBufferSpec = dstBufferStack.back();

    int retval = DoFill(dstBufferSpec, srcLayer->getColor());
    if (retval) {
      ALOGE("Got error code %d from DoFill function", retval);
    }

    srcLayerSpec = dstBufferSpec;
    dstBufferStack.pop_back();
  }

  // TODO(jemoreira): We are converting to ARGB as the first step under the
  // assumption that scaling ARGB is faster than scaling I420 (the most common).
  // This should be confirmed with testing.
  if (needsConversion) {
    BufferSpec& dstBufferSpec = dstBufferStack.back();
    if (needsScaling || needsTranspose) {
      // If a rotation or a scaling operation are needed the dimensions at the
      // top of the buffer stack are wrong (wrong sizes for scaling, swapped
      // width and height for 90 and 270 rotations).
      // Make width and height match the crop sizes on the source
      uint32_t srcWidth = srcLayerSpec.cropWidth;
      uint32_t srcHeight = srcLayerSpec.cropHeight;
      uint32_t dst_stride_bytes =
          AlignToPower2(srcWidth * dstBufferBytesPerPixel, 4);
      uint32_t neededSize = dst_stride_bytes * srcHeight;
      dstBufferSpec.width = srcWidth;
      dstBufferSpec.height = srcHeight;
      // Adjust the stride accordingly
      dstBufferSpec.strideBytes = dst_stride_bytes;
      // Crop sizes also need to be adjusted
      dstBufferSpec.cropWidth = srcWidth;
      dstBufferSpec.cropHeight = srcHeight;
      // cropX and y are fine at 0, format is already set to match destination

      // In case of a scale, the source frame may be bigger than the default tmp
      // buffer size
      dstBufferSpec.buffer =
          compositionIntermediateStorage.getSpecialScratchBuffer(neededSize);
    }

    int retval = DoConversion(srcLayerSpec, dstBufferSpec, needsVFlip);
    if (retval) {
      ALOGE("Got error code %d from DoConversion function", retval);
    }
    needsVFlip = false;
    srcLayerSpec = dstBufferSpec;
    dstBufferStack.pop_back();
  }

  if (needsScaling) {
    BufferSpec& dstBufferSpec = dstBufferStack.back();
    if (needsTranspose) {
      // If a rotation is needed, the temporary buffer has the correct size but
      // needs to be transposed and have its stride updated accordingly. The
      // crop sizes also needs to be transposed, but not the x and y since they
      // are both zero in a temporary buffer (and it is a temporary buffer
      // because a rotation will be performed next).
      std::swap(dstBufferSpec.width, dstBufferSpec.height);
      std::swap(dstBufferSpec.cropWidth, dstBufferSpec.cropHeight);
      // TODO (jemoreira): Aligment (To align here may cause the needed size to
      // be bigger than the buffer, so care should be taken)
      dstBufferSpec.strideBytes = dstBufferSpec.width * dstBufferBytesPerPixel;
    }
    int retval = DoScaling(srcLayerSpec, dstBufferSpec, needsVFlip);
    needsVFlip = false;
    if (retval) {
      ALOGE("Got error code %d from DoScaling function", retval);
    }
    srcLayerSpec = dstBufferSpec;
    dstBufferStack.pop_back();
  }

  if (needsRotation) {
    int retval =
        DoRotation(srcLayerSpec, dstBufferStack.back(), rotation, needsVFlip);
    needsVFlip = false;
    if (retval) {
      ALOGE("Got error code %d from DoTransform function", retval);
    }
    srcLayerSpec = dstBufferStack.back();
    dstBufferStack.pop_back();
  }

  if (needsAttenuation) {
    int retval = DoAttenuation(srcLayerSpec, dstBufferStack.back(), needsVFlip);
    needsVFlip = false;
    if (retval) {
      ALOGE("Got error code %d from DoBlending function", retval);
    }
    srcLayerSpec = dstBufferStack.back();
    dstBufferStack.pop_back();
  }

  if (needsBrightness) {
    int retval = DoBrightnessShading(srcLayerSpec, dstBufferStack.back(),
                                     srcLayer->getBrightness());
    if (retval) {
      ALOGE("Got error code %d from DoBrightnessShading function", retval);
    }
    srcLayerSpec = dstBufferStack.back();
    dstBufferStack.pop_back();
  }

  if (needsCopy) {
    int retval = DoCopy(srcLayerSpec, dstBufferStack.back(), needsVFlip);
    needsVFlip = false;
    if (retval) {
      ALOGE("Got error code %d from DoBlending function", retval);
    }
    srcLayerSpec = dstBufferStack.back();
    dstBufferStack.pop_back();
  }

  // Blending (if needed) should always be the last operation, so that it reads
  // and writes in the destination layer and not some temporary buffer.
  if (needsBlending) {
    int retval = DoBlending(srcLayerSpec, dstBufferStack.back(), needsVFlip);
    needsVFlip = false;
    if (retval) {
      ALOGE("Got error code %d from DoBlending function", retval);
    }
    // Don't need to assign destination to source in the last one
    dstBufferStack.pop_back();
  }

  return HWC3::Error::None;
}

HWC3::Error GuestFrameComposer::applyColorTransformToRGBA(
    const std::array<float, 16>& transfromMatrix,  //
    std::uint8_t* buffer,                          //
    std::uint32_t bufferWidth,                     //
    std::uint32_t bufferHeight,                    //
    std::uint32_t bufferStrideBytes) {
  ATRACE_CALL();

  const auto transformMatrixLibyuv = ToLibyuvColorMatrix(transfromMatrix);
  libyuv::ARGBColorMatrix(buffer, static_cast<int>(bufferStrideBytes),  //
                          buffer, static_cast<int>(bufferStrideBytes),  //
                          transformMatrixLibyuv.data(),                 //
                          static_cast<int>(bufferWidth),                //
                          static_cast<int>(bufferHeight));

  return HWC3::Error::None;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
