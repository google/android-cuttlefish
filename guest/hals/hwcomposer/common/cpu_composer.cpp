/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "guest/hals/hwcomposer/common/cpu_composer.h"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

#include <drm_fourcc.h>
#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>
#include <libyuv.h>
#include <log/log.h>

#include "common/libs/utils/size_utils.h"
#include "guest/hals/hwcomposer/common/drm_utils.h"
#include "guest/hals/hwcomposer/common/geometry_utils.h"

namespace cuttlefish {

namespace {

bool LayerNeedsScaling(const hwc_layer_1_t& layer) {
  int from_w = layer.sourceCrop.right - layer.sourceCrop.left;
  int from_h = layer.sourceCrop.bottom - layer.sourceCrop.top;
  int to_w = layer.displayFrame.right - layer.displayFrame.left;
  int to_h = layer.displayFrame.bottom - layer.displayFrame.top;

  bool not_rot_scale = from_w != to_w || from_h != to_h;
  bool rot_scale = from_w != to_h || from_h != to_w;

  bool needs_rot = layer.transform & HAL_TRANSFORM_ROT_90;

  return needs_rot ? rot_scale : not_rot_scale;
}

bool LayerNeedsBlending(const hwc_layer_1_t& layer) {
  return layer.blending != HWC_BLENDING_NONE;
}

bool LayerNeedsAttenuation(const hwc_layer_1_t& layer) {
  return layer.blending == HWC_BLENDING_COVERAGE;
}

struct BufferSpec;
typedef int (*ConverterFunction)(const BufferSpec& src, const BufferSpec& dst,
                                 bool v_flip);
int DoCopy(const BufferSpec& src, const BufferSpec& dst, bool v_flip);
int ConvertFromYV12(const BufferSpec& src, const BufferSpec& dst, bool v_flip);

ConverterFunction GetConverterForDrmFormat(uint32_t drm_format) {
  switch (drm_format) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
      return &DoCopy;
    case DRM_FORMAT_YVU420:
      return &ConvertFromYV12;
  }
  ALOGW("Unsupported format: %d(%s), returning null converter",
        drm_format, GetDrmFormatString(drm_format));
  return nullptr;
}

bool IsDrmFormatSupported(uint32_t drm_format) {
  return GetConverterForDrmFormat(drm_format) != nullptr;
}

/*******************************************************************************
Libyuv's convert functions only allow the combination of any rotation (multiple
of 90 degrees) and a vertical flip, but not horizontal flips.
Surfaceflinger's transformations are expressed in terms of a vertical flip, a
horizontal flip and/or a single 90 degrees clockwise rotation (see
NATIVE_WINDOW_TRANSFORM_HINT documentation on system/window.h for more insight).
The following code allows to turn a horizontal flip into a 180 degrees rotation
and a vertical flip.
*******************************************************************************/
libyuv::RotationMode GetRotationFromTransform(uint32_t transform) {
  uint32_t rotation =
      (transform & HAL_TRANSFORM_ROT_90) ? 1 : 0;          // 1 * ROT90 bit
  rotation += (transform & HAL_TRANSFORM_FLIP_H) ? 2 : 0;  // 2 * VFLIP bit
  return static_cast<libyuv::RotationMode>(90 * rotation);
}

bool GetVFlipFromTransform(uint32_t transform) {
  // vertical flip xor horizontal flip
  return ((transform & HAL_TRANSFORM_FLIP_V) >> 1) ^
         (transform & HAL_TRANSFORM_FLIP_H);
}

struct BufferSpec {
  uint8_t* buffer;
  std::optional<android_ycbcr> buffer_ycbcr;
  int width;
  int height;
  int crop_x;
  int crop_y;
  int crop_width;
  int crop_height;
  uint32_t drm_format;
  int stride_bytes;
  int sample_bytes;

  BufferSpec(uint8_t* buffer,
             std::optional<android_ycbcr> buffer_ycbcr,
             int width,
             int height,
             int crop_x,
             int crop_y,
             int crop_width,
             int crop_height,
             uint32_t drm_format,
             int stride_bytes,
             int sample_bytes)
      : buffer(buffer),
        buffer_ycbcr(buffer_ycbcr),
        width(width),
        height(height),
        crop_x(crop_x),
        crop_y(crop_y),
        crop_width(crop_width),
        crop_height(crop_height),
        drm_format(drm_format),
        stride_bytes(stride_bytes),
        sample_bytes(sample_bytes) {}

  BufferSpec(uint8_t* buffer,
             int width,
             int height,
             int stride_bytes)
      : BufferSpec(buffer,
                   /*buffer_ycbcr=*/std::nullopt,
                   width,
                   height,
                   /*crop_x=*/0,
                   /*crop_y=*/0,
                   /*crop_width=*/width,
                   /*crop_height=*/height,
                   /*drm_format=*/DRM_FORMAT_ABGR8888,
                   stride_bytes,
                   /*sample_bytes=*/4) {}

};

int ConvertFromYV12(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  // The following calculation of plane offsets and alignments are based on
  // swiftshader's Sampler::setTextureLevel() implementation
  // (Renderer/Sampler.cpp:225)

  auto& src_buffer_ycbcr_opt = src.buffer_ycbcr;
  if (!src_buffer_ycbcr_opt) {
    ALOGE("%s called on non ycbcr buffer", __FUNCTION__);
    return -1;
  }
  auto& src_buffer_ycbcr = *src_buffer_ycbcr_opt;

  // The libyuv::I420ToARGB() function is for tri-planar.
  if (src_buffer_ycbcr.chroma_step != 1) {
    ALOGE("%s called with bad chroma step", __FUNCTION__);
    return -1;
  }

  uint8_t* src_y = reinterpret_cast<uint8_t*>(src_buffer_ycbcr.y);
  int stride_y = src_buffer_ycbcr.ystride;
  uint8_t* src_u = reinterpret_cast<uint8_t*>(src_buffer_ycbcr.cb);
  int stride_u = src_buffer_ycbcr.cstride;
  uint8_t* src_v = reinterpret_cast<uint8_t*>(src_buffer_ycbcr.cr);
  int stride_v = src_buffer_ycbcr.cstride;

  // Adjust for crop
  src_y += src.crop_y * stride_y + src.crop_x;
  src_v += (src.crop_y / 2) * stride_v + (src.crop_x / 2);
  src_u += (src.crop_y / 2) * stride_u + (src.crop_x / 2);
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride_bytes +
                        dst.crop_x * dst.sample_bytes;

  // YV12 is the same as I420, with the U and V planes swapped
  return libyuv::I420ToARGB(src_y, stride_y,
                            src_v, stride_v,
                            src_u, stride_u,
                            dst_buffer, dst.stride_bytes,
                            dst.crop_width,
                            v_flip ? -dst.crop_height : dst.crop_height);
}

int DoConversion(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  return (*GetConverterForDrmFormat(src.drm_format))(src, dst, v_flip);
}

int DoCopy(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  // Point to the upper left corner of the crop rectangle
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride_bytes +
                        src.crop_x * src.sample_bytes;
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride_bytes +
                        dst.crop_x * dst.sample_bytes;
  int width = src.crop_width;
  int height = src.crop_height;

  if (v_flip) {
    height = -height;
  }

  // HAL formats are named based on the order of the pixel componets on the
  // byte stream, while libyuv formats are named based on the order of those
  // pixel components in an integer written from left to right. So
  // libyuv::FOURCC_ARGB is equivalent to HAL_PIXEL_FORMAT_BGRA_8888.
  auto ret = libyuv::ARGBCopy(src_buffer, src.stride_bytes,
                              dst_buffer, dst.stride_bytes,
                              width, height);
  return ret;
}

int DoRotation(const BufferSpec& src, const BufferSpec& dst,
               libyuv::RotationMode rotation, bool v_flip) {
  // Point to the upper left corner of the crop rectangles
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride_bytes +
                        src.crop_x * src.sample_bytes;
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride_bytes +
                        dst.crop_x * dst.sample_bytes;
  int width = src.crop_width;
  int height = src.crop_height;

  if (v_flip) {
    height = -height;
  }

  return libyuv::ARGBRotate(src_buffer, src.stride_bytes,
                            dst_buffer, dst.stride_bytes,
                            width, height, rotation);
}

int DoScaling(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  // Point to the upper left corner of the crop rectangles
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride_bytes +
                        src.crop_x * src.sample_bytes;
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride_bytes +
                        dst.crop_x * dst.sample_bytes;
  int src_width = src.crop_width;
  int src_height = src.crop_height;
  int dst_width = dst.crop_width;
  int dst_height = dst.crop_height;

  if (v_flip) {
    src_height = -src_height;
  }

  return libyuv::ARGBScale(src_buffer, src.stride_bytes, src_width, src_height,
                           dst_buffer, dst.stride_bytes, dst_width, dst_height,
                           libyuv::kFilterBilinear);
}

int DoAttenuation(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  // Point to the upper left corner of the crop rectangles
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride_bytes +
                        src.crop_x * src.sample_bytes;
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride_bytes +
                        dst.crop_x * dst.sample_bytes;
  int width = dst.crop_width;
  int height = dst.crop_height;

  if (v_flip) {
    height = -height;
  }

  return libyuv::ARGBAttenuate(src_buffer, src.stride_bytes,
                               dst_buffer, dst.stride_bytes,
                               width, height);
}

int DoBlending(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  // Point to the upper left corner of the crop rectangles
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride_bytes +
                        src.crop_x * src.sample_bytes;
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride_bytes +
                        dst.crop_x * dst.sample_bytes;
  int width = dst.crop_width;
  int height = dst.crop_height;

  if (v_flip) {
    height = -height;
  }

  // libyuv's ARGB format is hwcomposer's BGRA format, since blending only cares
  // for the position of alpha in the pixel and not the position of the colors
  // this function is perfectly usable.
  return libyuv::ARGBBlend(src_buffer, src.stride_bytes,
                           dst_buffer, dst.stride_bytes,
                           dst_buffer, dst.stride_bytes,
                           width, height);
}

std::optional<BufferSpec> GetBufferSpec(GrallocBuffer& buffer,
                                        const hwc_rect_t& buffer_crop) {
  auto buffer_format_opt = buffer.GetDrmFormat();
  if (!buffer_format_opt) {
    ALOGE("Failed to get gralloc buffer format.");
    return std::nullopt;
  }
  uint32_t buffer_format = *buffer_format_opt;

  auto buffer_width_opt = buffer.GetWidth();
  if (!buffer_width_opt) {
    ALOGE("Failed to get gralloc buffer width.");
    return std::nullopt;
  }
  uint32_t buffer_width = *buffer_width_opt;

  auto buffer_height_opt = buffer.GetHeight();
  if (!buffer_height_opt) {
    ALOGE("Failed to get gralloc buffer height.");
    return std::nullopt;
  }
  uint32_t buffer_height = *buffer_height_opt;

  uint8_t* buffer_data = nullptr;
  uint32_t buffer_stride_bytes = 0;
  std::optional<android_ycbcr> buffer_ycbcr_data;

  if (buffer_format == DRM_FORMAT_NV12 ||
      buffer_format == DRM_FORMAT_NV21 ||
      buffer_format == DRM_FORMAT_YVU420) {
    buffer_ycbcr_data = buffer.LockYCbCr();
    if (!buffer_ycbcr_data) {
      ALOGE("%s failed to lock gralloc buffer.", __FUNCTION__);
      return std::nullopt;
    }
  } else {
    auto buffer_data_opt = buffer.Lock();
    if (!buffer_data_opt) {
      ALOGE("%s failed to lock gralloc buffer.", __FUNCTION__);
      return std::nullopt;
    }
    buffer_data = reinterpret_cast<uint8_t*>(*buffer_data_opt);

    auto buffer_stride_bytes_opt = buffer.GetMonoPlanarStrideBytes();
    if (!buffer_stride_bytes_opt) {
      ALOGE("%s failed to get plane stride.", __FUNCTION__);
      return std::nullopt;
    }
    buffer_stride_bytes = *buffer_stride_bytes_opt;
  }

  return BufferSpec(
      buffer_data,
      buffer_ycbcr_data,
      buffer_width,
      buffer_height,
      buffer_crop.left,
      buffer_crop.top,
      buffer_crop.right - buffer_crop.left,
      buffer_crop.bottom - buffer_crop.top,
      buffer_format,
      buffer_stride_bytes,
      GetDrmFormatBytesPerPixel(buffer_format));
}

}  // namespace

bool CpuComposer::CanCompositeLayer(const hwc_layer_1_t& layer) {
  buffer_handle_t buffer_handle = layer.handle;
  if (buffer_handle == nullptr) {
    ALOGW("%s received a layer with a null handle", __FUNCTION__);
    return false;
  }

  auto buffer_opt = gralloc_.Import(buffer_handle);
  if (!buffer_opt) {
    ALOGE("Failed to import layer buffer.");
    return false;
  }
  GrallocBuffer& buffer = *buffer_opt;

  auto buffer_format_opt = buffer.GetDrmFormat();
  if (!buffer_format_opt) {
    ALOGE("Failed to get layer buffer format.");
    return false;
  }
  uint32_t buffer_format = *buffer_format_opt;

  if (!IsDrmFormatSupported(buffer_format)) {
    ALOGD("Unsupported pixel format: 0x%x, doing software composition instead",
          buffer_format);
    return false;
  }
  return true;
}

void CpuComposer::CompositeLayer(hwc_layer_1_t* src_layer, int buffer_idx) {
  libyuv::RotationMode rotation =
      GetRotationFromTransform(src_layer->transform);

  auto src_imported_buffer_opt = gralloc_.Import(src_layer->handle);
  if (!src_imported_buffer_opt) {
    ALOGE("Failed to import layer buffer.");
    return;
  }
  GrallocBuffer& src_imported_buffer = *src_imported_buffer_opt;

  auto src_layer_spec_opt = GetBufferSpec(src_imported_buffer, src_layer->sourceCrop);
  if (!src_layer_spec_opt) {
    return;
  }
  BufferSpec src_layer_spec = *src_layer_spec_opt;

  // TODO(jemoreira): Remove the hardcoded fomat.
  bool needs_conversion = src_layer_spec.drm_format != DRM_FORMAT_XBGR8888;
  bool needs_scaling = LayerNeedsScaling(*src_layer);
  bool needs_rotation = rotation != libyuv::kRotate0;
  bool needs_transpose = needs_rotation && rotation != libyuv::kRotate180;
  bool needs_vflip = GetVFlipFromTransform(src_layer->transform);
  bool needs_attenuation = LayerNeedsAttenuation(*src_layer);
  bool needs_blending = LayerNeedsBlending(*src_layer);
  bool needs_copy = !(needs_conversion || needs_scaling || needs_rotation ||
                      needs_vflip || needs_attenuation || needs_blending);

  uint8_t* dst_buffer =
    reinterpret_cast<uint8_t*>(screen_view_->GetBuffer(buffer_idx));

  BufferSpec dst_layer_spec(
      dst_buffer,
      /*buffer_ycbcr=*/std::nullopt,
      screen_view_->x_res(),
      screen_view_->y_res(),
      src_layer->displayFrame.left,
      src_layer->displayFrame.top,
      src_layer->displayFrame.right - src_layer->displayFrame.left,
      src_layer->displayFrame.bottom - src_layer->displayFrame.top,
      DRM_FORMAT_XBGR8888,
      screen_view_->line_length(),
      4);

  // Add the destination layer to the bottom of the buffer stack
  std::vector<BufferSpec> dest_buffer_stack(1, dst_layer_spec);

  // If more than operation is to be performed, a temporary buffer is needed for
  // each additional operation

  // N operations need N destination buffers, the destination layer (the
  // framebuffer) is one of them, so only N-1 temporary buffers are needed.
  // Vertical flip is not taken into account because it can be done together
  // with any other operation.
  int needed_tmp_buffers = (needs_conversion ? 1 : 0) +
                           (needs_scaling ? 1 : 0) + (needs_rotation ? 1 : 0) +
                           (needs_attenuation ? 1 : 0) +
                           (needs_blending ? 1 : 0) + (needs_copy ? 1 : 0) - 1;

  int tmp_buffer_width =
      src_layer->displayFrame.right - src_layer->displayFrame.left;
  int tmp_buffer_height =
      src_layer->displayFrame.bottom - src_layer->displayFrame.top;
  int tmp_buffer_stride_bytes =
      cuttlefish::AlignToPowerOf2(tmp_buffer_width * screen_view_->bytes_per_pixel(), 4);

  for (int i = 0; i < needed_tmp_buffers; i++) {
    BufferSpec tmp_buffer_spec(
        RotateTmpBuffer(i),
        tmp_buffer_width,
        tmp_buffer_height,
        tmp_buffer_stride_bytes);
    dest_buffer_stack.push_back(tmp_buffer_spec);
  }

  // Conversion and scaling should always be the first operations, so that every
  // other operation works on equally sized frames (garanteed to fit in the tmp
  // buffers)

  // TODO(jemoreira): We are converting to ARGB as the first step under the
  // assumption that scaling ARGB is faster than scaling I420 (the most common).
  // This should be confirmed with testing.
  if (needs_conversion) {
    BufferSpec& dst_buffer_spec = dest_buffer_stack.back();
    if (needs_scaling || needs_transpose) {
      // If a rotation or a scaling operation are needed the dimensions at the
      // top of the buffer stack are wrong (wrong sizes for scaling, swapped
      // width and height for 90 and 270 rotations).
      // Make width and height match the crop sizes on the source
      int src_width = src_layer_spec.crop_width;
      int src_height = src_layer_spec.crop_height;
      int dst_stride_bytes =
          cuttlefish::AlignToPowerOf2(src_width * screen_view_->bytes_per_pixel(), 4);
      size_t needed_size = dst_stride_bytes * src_height;
      dst_buffer_spec.width = src_width;
      dst_buffer_spec.height = src_height;
      // Adjust the stride accordingly
      dst_buffer_spec.stride_bytes = dst_stride_bytes;
      // Crop sizes also need to be adjusted
      dst_buffer_spec.crop_width = src_width;
      dst_buffer_spec.crop_height = src_height;
      // crop_x and y are fine at 0, format is already set to match destination

      // In case of a scale, the source frame may be bigger than the default tmp
      // buffer size
      if (needed_size > tmp_buffer_.size() / kNumTmpBufferPieces) {
        dst_buffer_spec.buffer = GetSpecialTmpBuffer(needed_size);
      }
    }

    int retval = DoConversion(src_layer_spec, dst_buffer_spec, needs_vflip);
    if (retval) {
      ALOGE("Got error code %d from DoConversion function", retval);
    }
    needs_vflip = false;
    src_layer_spec = dst_buffer_spec;
    dest_buffer_stack.pop_back();
  }

  if (needs_scaling) {
    BufferSpec& dst_buffer_spec = dest_buffer_stack.back();
    if (needs_transpose) {
      // If a rotation is needed, the temporary buffer has the correct size but
      // needs to be transposed and have its stride updated accordingly. The
      // crop sizes also needs to be transposed, but not the x and y since they
      // are both zero in a temporary buffer (and it is a temporary buffer
      // because a rotation will be performed next).
      std::swap(dst_buffer_spec.width, dst_buffer_spec.height);
      std::swap(dst_buffer_spec.crop_width, dst_buffer_spec.crop_height);
      // TODO (jemoreira): Aligment (To align here may cause the needed size to
      // be bigger than the buffer, so care should be taken)
      dst_buffer_spec.stride_bytes =
          dst_buffer_spec.width * screen_view_->bytes_per_pixel();
    }
    int retval = DoScaling(src_layer_spec, dst_buffer_spec, needs_vflip);
    needs_vflip = false;
    if (retval) {
      ALOGE("Got error code %d from DoScaling function", retval);
    }
    src_layer_spec = dst_buffer_spec;
    dest_buffer_stack.pop_back();
  }

  if (needs_rotation) {
    int retval = DoRotation(src_layer_spec, dest_buffer_stack.back(), rotation,
                            needs_vflip);
    needs_vflip = false;
    if (retval) {
      ALOGE("Got error code %d from DoTransform function", retval);
    }
    src_layer_spec = dest_buffer_stack.back();
    dest_buffer_stack.pop_back();
  }

  if (needs_attenuation) {
    int retval = DoAttenuation(src_layer_spec, dest_buffer_stack.back(),
                               needs_vflip);
    needs_vflip = false;
    if (retval) {
      ALOGE("Got error code %d from DoBlending function", retval);
    }
    src_layer_spec = dest_buffer_stack.back();
    dest_buffer_stack.pop_back();
  }

  if (needs_copy) {
    int retval = DoCopy(src_layer_spec, dest_buffer_stack.back(), needs_vflip);
    needs_vflip = false;
    if (retval) {
      ALOGE("Got error code %d from DoBlending function", retval);
    }
    src_layer_spec = dest_buffer_stack.back();
    dest_buffer_stack.pop_back();
  }

  // Blending (if needed) should always be the last operation, so that it reads
  // and writes in the destination layer and not some temporary buffer.
  if (needs_blending) {
    int retval = DoBlending(src_layer_spec, dest_buffer_stack.back(),
                            needs_vflip);
    needs_vflip = false;
    if (retval) {
      ALOGE("Got error code %d from DoBlending function", retval);
    }
    // Don't need to assign destination to source in the last one
    dest_buffer_stack.pop_back();
  }

  src_imported_buffer.Unlock();
}

/* static */ const int CpuComposer::kNumTmpBufferPieces = 2;

CpuComposer::CpuComposer(std::unique_ptr<ScreenView> screen_view)
    : BaseComposer(std::move(screen_view)),
      tmp_buffer_(kNumTmpBufferPieces * screen_view_->buffer_size()) {}

int CpuComposer::PrepareLayers(size_t num_layers, hwc_layer_1_t* layers) {
  int composited_layers_count = 0;

  // Loop over layers in inverse order of z-index
  for (size_t layer_index = num_layers; layer_index > 0;) {
    // Decrement here to be able to compare unsigned integer with 0 in the
    // loop condition
    --layer_index;
    if (IS_TARGET_FRAMEBUFFER(layers[layer_index].compositionType)) {
      continue;
    }
    if (layers[layer_index].flags & HWC_SKIP_LAYER) {
      continue;
    }
    if (layers[layer_index].compositionType == HWC_BACKGROUND) {
      layers[layer_index].compositionType = HWC_FRAMEBUFFER;
      continue;
    }
    layers[layer_index].compositionType = HWC_OVERLAY;
    // Hwcomposer cannot draw below software-composed layers, so we need
    // to mark those HWC_FRAMEBUFFER as well.
    for (size_t top_idx = layer_index + 1; top_idx < num_layers; ++top_idx) {
      // layers marked as skip are in a state that makes them unreliable to
      // read, so it's best to assume they cover the whole screen
      if (layers[top_idx].flags & HWC_SKIP_LAYER ||
          (layers[top_idx].compositionType == HWC_FRAMEBUFFER &&
           LayersOverlap(layers[layer_index], layers[top_idx]))) {
        layers[layer_index].compositionType = HWC_FRAMEBUFFER;
        break;
      }
    }
    if (layers[layer_index].compositionType == HWC_OVERLAY &&
        !CanCompositeLayer(layers[layer_index])) {
      layers[layer_index].compositionType = HWC_FRAMEBUFFER;
    }
    if (layers[layer_index].compositionType == HWC_OVERLAY) {
      ++composited_layers_count;
    }
  }
  return composited_layers_count;
}

int CpuComposer::SetLayers(size_t num_layers, hwc_layer_1_t* layers) {
  int targetFbs = 0;
  int buffer_idx = screen_view_->NextBuffer();

  // The framebuffer target layer should be composed if at least one layers was
  // marked HWC_FRAMEBUFFER or if it's the only layer in the composition
  // (unlikely)
  bool fb_target = true;
  for (size_t idx = 0; idx < num_layers; idx++) {
    if (layers[idx].compositionType == HWC_FRAMEBUFFER) {
      // At least one was found
      fb_target = true;
      break;
    }
    if (layers[idx].compositionType == HWC_OVERLAY) {
      // Not the only layer in the composition
      fb_target = false;
    }
  }

  // When the framebuffer target needs to be composed, it has to go first.
  if (fb_target) {
    for (size_t idx = 0; idx < num_layers; idx++) {
      if (IS_TARGET_FRAMEBUFFER(layers[idx].compositionType)) {
        CompositeLayer(&layers[idx], buffer_idx);
        break;
      }
    }
  }

  for (size_t idx = 0; idx < num_layers; idx++) {
    if (IS_TARGET_FRAMEBUFFER(layers[idx].compositionType)) {
      ++targetFbs;
    }
    if (layers[idx].compositionType == HWC_OVERLAY &&
        !(layers[idx].flags & HWC_SKIP_LAYER)) {
      CompositeLayer(&layers[idx], buffer_idx);
    }
  }
  if (targetFbs != 1) {
    ALOGW("Saw %zu layers, posted=%d", num_layers, targetFbs);
  }
  screen_view_->Broadcast(buffer_idx);
  return 0;
}

uint8_t* CpuComposer::RotateTmpBuffer(unsigned int order) {
  return &tmp_buffer_[(order % kNumTmpBufferPieces) * tmp_buffer_.size() /
                      kNumTmpBufferPieces];
}

uint8_t* CpuComposer::GetSpecialTmpBuffer(size_t needed_size) {
  special_tmp_buffer_.resize(needed_size);
  return &special_tmp_buffer_[0];
}

}  // namespace cuttlefish
