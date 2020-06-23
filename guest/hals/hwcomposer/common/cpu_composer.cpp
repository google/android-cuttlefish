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

#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>
#include <libyuv.h>
#include <log/log.h>
#include <system/graphics.h>

#include "common/libs/utils/size_utils.h"
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
ConverterFunction GetConverter(uint32_t format) {
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
      return &DoCopy;

    case HAL_PIXEL_FORMAT_YV12:
      return &ConvertFromYV12;

    // Unsupported formats
    // TODO(jemoreira): Conversion from these formats should be implemented as
    // we find evidence of its usage.
    // case HAL_PIXEL_FORMAT_BGRA_8888:

    // case HAL_PIXEL_FORMAT_RGB_888:
    // case HAL_PIXEL_FORMAT_RGB_565:

    // case HAL_PIXEL_FORMAT_sRGB_A_8888:
    // case HAL_PIXEL_FORMAT_sRGB_X_8888:

    // case HAL_PIXEL_FORMAT_Y8:
    // case HAL_PIXEL_FORMAT_Y16:

    // case HAL_PIXEL_FORMAT_RAW_SENSOR:
    // case HAL_PIXEL_FORMAT_BLOB:

    // case HAL_PIXEL_FORMAT_YCbCr_420_888:
    // case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    // case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    // case HAL_PIXEL_FORMAT_YCbCr_422_I:
    default:
      ALOGW("Unsupported format: 0x%04x, returning null converter function",
            format);
  }
  return NULL;
}

// Whether we support a given format
bool IsFormatSupported(uint32_t format) { return GetConverter(format) != NULL; }

bool CanCompositeLayer(const hwc_layer_1_t& layer) {
  if (layer.handle == NULL) {
    ALOGW("%s received a layer with a null handler", __FUNCTION__);
    return false;
  }
  int format = reinterpret_cast<const private_handle_t*>(layer.handle)->format;
  if (!IsFormatSupported(format)) {
    ALOGD("Unsupported pixel format: 0x%x, doing software composition instead",
          format);
    return false;
  }
  return true;
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
  size_t size;
  int width;
  int height;
  int stride;
  int crop_x;
  int crop_y;
  int crop_width;
  int crop_height;
  uint32_t format;

  BufferSpec(uint8_t* buffer, size_t size, int width, int height, int stride)
      : buffer(buffer),
        size(size),
        width(width),
        height(height),
        stride(stride),
        crop_x(0),
        crop_y(0),
        crop_width(width),
        crop_height(height),
        format(HAL_PIXEL_FORMAT_RGBA_8888) {}
};

int ConvertFromYV12(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  // use the stride in pixels as the source width
  int stride_in_pixels = src.stride / formatToBytesPerPixel(src.format);

  // The following calculation of plane offsets and alignments are based on
  // swiftshader's Sampler::setTextureLevel() implementation
  // (Renderer/Sampler.cpp:225)
  uint8_t* src_y = src.buffer;
  int stride_y = stride_in_pixels;
  uint8_t* src_v = src_y + stride_y * src.height;
  int stride_v = cuttlefish::AlignToPowerOf2(stride_y / 2, 4);
  uint8_t* src_u = src_v + stride_v * src.height / 2;
  int stride_u = cuttlefish::AlignToPowerOf2(stride_y / 2, 4);

  // Adjust for crop
  src_y += src.crop_y * stride_y + src.crop_x;
  src_v += (src.crop_y / 2) * stride_v + (src.crop_x / 2);
  src_u += (src.crop_y / 2) * stride_u + (src.crop_x / 2);
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride +
                        dst.crop_x * formatToBytesPerPixel(dst.format);

  // YV12 is the same as I420, with the U and V planes swapped
  return libyuv::I420ToARGB(src_y, stride_y, src_v, stride_v, src_u, stride_u,
                            dst_buffer, dst.stride, dst.crop_width,
                            v_flip ? -dst.crop_height : dst.crop_height);
}

int DoConversion(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  return (*GetConverter(src.format))(src, dst, v_flip);
}

int DoCopy(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  // Point to the upper left corner of the crop rectangle
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride +
                        src.crop_x * formatToBytesPerPixel(src.format);
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride +
                        dst.crop_x * formatToBytesPerPixel(dst.format);
  int width = src.crop_width;
  int height = src.crop_height;

  if (v_flip) {
    height = -height;
  }

  // HAL formats are named based on the order of the pixel componets on the
  // byte stream, while libyuv formats are named based on the order of those
  // pixel components in an integer written from left to right. So
  // libyuv::FOURCC_ARGB is equivalent to HAL_PIXEL_FORMAT_BGRA_8888.
  return libyuv::ARGBCopy(src_buffer, src.stride, dst_buffer, dst.stride, width,
                          height);
}

int DoRotation(const BufferSpec& src, const BufferSpec& dst,
               libyuv::RotationMode rotation, bool v_flip) {
  // Point to the upper left corner of the crop rectangles
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride +
                        src.crop_x * formatToBytesPerPixel(src.format);
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride +
                        dst.crop_x * formatToBytesPerPixel(dst.format);
  int width = src.crop_width;
  int height = src.crop_height;

  if (v_flip) {
    height = -height;
  }

  return libyuv::ARGBRotate(src_buffer, src.stride, dst_buffer, dst.stride,
                            width, height, rotation);
}

int DoScaling(const BufferSpec& src, const BufferSpec& dst, bool v_flip) {
  // Point to the upper left corner of the crop rectangles
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride +
                        src.crop_x * formatToBytesPerPixel(src.format);
  uint8_t* dst_buffer = dst.buffer + dst.crop_y * dst.stride +
                        dst.crop_x * formatToBytesPerPixel(dst.format);
  int src_width = src.crop_width;
  int src_height = src.crop_height;
  int dst_width = dst.crop_width;
  int dst_height = dst.crop_height;

  if (v_flip) {
    src_height = -src_height;
  }

  return libyuv::ARGBScale(src_buffer, src.stride, src_width, src_height,
                           dst_buffer, dst.stride, dst_width, dst_height,
                           libyuv::kFilterBilinear);
}

int DoAttenuation(const BufferSpec& src, const BufferSpec& dest, bool v_flip) {
  // Point to the upper left corner of the crop rectangles
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride +
                        src.crop_x * formatToBytesPerPixel(src.format);
  uint8_t* dst_buffer = dest.buffer + dest.crop_y * dest.stride +
                        dest.crop_x * formatToBytesPerPixel(dest.format);
  int width = dest.crop_width;
  int height = dest.crop_height;

  if (v_flip) {
    height = -height;
  }

  return libyuv::ARGBAttenuate(src_buffer, src.stride, dst_buffer, dest.stride,
                               width, height);
}

int DoBlending(const BufferSpec& src, const BufferSpec& dest, bool v_flip) {
  // Point to the upper left corner of the crop rectangles
  uint8_t* src_buffer = src.buffer + src.crop_y * src.stride +
                        src.crop_x * formatToBytesPerPixel(src.format);
  uint8_t* dst_buffer = dest.buffer + dest.crop_y * dest.stride +
                        dest.crop_x * formatToBytesPerPixel(dest.format);
  int width = dest.crop_width;
  int height = dest.crop_height;

  if (v_flip) {
    height = -height;
  }

  // libyuv's ARGB format is hwcomposer's BGRA format, since blending only cares
  // for the position of alpha in the pixel and not the position of the colors
  // this function is perfectly usable.
  return libyuv::ARGBBlend(src_buffer, src.stride, dst_buffer, dest.stride,
                           dst_buffer, dest.stride, width, height);
}

}  // namespace

void CpuComposer::CompositeLayer(hwc_layer_1_t* src_layer, int buffer_idx) {
  libyuv::RotationMode rotation =
      GetRotationFromTransform(src_layer->transform);

  const private_handle_t* src_priv_handle =
      reinterpret_cast<const private_handle_t*>(src_layer->handle);

  // TODO(jemoreira): Remove the hardcoded fomat.
  bool needs_conversion = src_priv_handle->format != HAL_PIXEL_FORMAT_RGBX_8888;
  bool needs_scaling = LayerNeedsScaling(*src_layer);
  bool needs_rotation = rotation != libyuv::kRotate0;
  bool needs_transpose = needs_rotation && rotation != libyuv::kRotate180;
  bool needs_vflip = GetVFlipFromTransform(src_layer->transform);
  bool needs_attenuation = LayerNeedsAttenuation(*src_layer);
  bool needs_blending = LayerNeedsBlending(*src_layer);
  bool needs_copy = !(needs_conversion || needs_scaling || needs_rotation ||
                      needs_vflip || needs_attenuation || needs_blending);

  uint8_t* src_buffer;
  uint8_t* dst_buffer =
      reinterpret_cast<uint8_t*>(screen_view_->GetBuffer(buffer_idx));
  int retval = gralloc_module_->lock(
      gralloc_module_, src_layer->handle, GRALLOC_USAGE_SW_READ_OFTEN, 0, 0,
      src_priv_handle->x_res, src_priv_handle->y_res,
      reinterpret_cast<void**>(&src_buffer));
  if (retval) {
    ALOGE("Got error code %d from lock function", retval);
    return;
  }
  if (retval) {
    ALOGE("Got error code %d from lock function", retval);
    // TODO(jemoreira): Use a lock_guard-like object.
    gralloc_module_->unlock(gralloc_module_, src_priv_handle);
    return;
  }

  BufferSpec src_layer_spec(src_buffer, src_priv_handle->total_size,
                            src_priv_handle->x_res, src_priv_handle->y_res,
                            src_priv_handle->stride_in_pixels *
                                formatToBytesPerPixel(src_priv_handle->format));
  src_layer_spec.crop_x = src_layer->sourceCrop.left;
  src_layer_spec.crop_y = src_layer->sourceCrop.top;
  src_layer_spec.crop_width =
      src_layer->sourceCrop.right - src_layer->sourceCrop.left;
  src_layer_spec.crop_height =
      src_layer->sourceCrop.bottom - src_layer->sourceCrop.top;
  src_layer_spec.format = src_priv_handle->format;

  BufferSpec dst_layer_spec(dst_buffer, screen_view_->buffer_size(),
                            screen_view_->x_res(), screen_view_->y_res(),
                            screen_view_->line_length());
  dst_layer_spec.crop_x = src_layer->displayFrame.left;
  dst_layer_spec.crop_y = src_layer->displayFrame.top;
  dst_layer_spec.crop_width =
      src_layer->displayFrame.right - src_layer->displayFrame.left;
  dst_layer_spec.crop_height =
      src_layer->displayFrame.bottom - src_layer->displayFrame.top;
  // TODO(jemoreira): Remove the hardcoded fomat.
  dst_layer_spec.format = HAL_PIXEL_FORMAT_RGBX_8888;

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

  int x_res = src_layer->displayFrame.right - src_layer->displayFrame.left;
  int y_res = src_layer->displayFrame.bottom - src_layer->displayFrame.top;
  size_t output_frame_size =
      x_res * cuttlefish::AlignToPowerOf2(y_res * screen_view_->bytes_per_pixel(), 4);
  while (needed_tmp_buffers > 0) {
    BufferSpec tmp(
        RotateTmpBuffer(needed_tmp_buffers), output_frame_size, x_res, y_res,
        cuttlefish::AlignToPowerOf2(x_res * screen_view_->bytes_per_pixel(), 4));
    dest_buffer_stack.push_back(tmp);
    needed_tmp_buffers--;
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
      int dst_stride =
          cuttlefish::AlignToPowerOf2(src_width * screen_view_->bytes_per_pixel(), 4);
      size_t needed_size = dst_stride * src_height;
      dst_buffer_spec.width = src_width;
      dst_buffer_spec.height = src_height;
      // Ajust the stride accordingly
      dst_buffer_spec.stride = dst_stride;
      // Crop sizes also need to be adjusted
      dst_buffer_spec.crop_width = src_width;
      dst_buffer_spec.crop_height = src_height;
      dst_buffer_spec.size = needed_size;
      // crop_x and y are fine at 0, format is already set to match destination

      // In case of a scale, the source frame may be bigger than the default tmp
      // buffer size
      if (needed_size > tmp_buffer_.size() / kNumTmpBufferPieces) {
        dst_buffer_spec.buffer = GetSpecialTmpBuffer(needed_size);
      }
    }
    retval = DoConversion(src_layer_spec, dst_buffer_spec, needs_vflip);
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
      dst_buffer_spec.stride =
          dst_buffer_spec.width * screen_view_->bytes_per_pixel();
    }
    retval = DoScaling(src_layer_spec, dst_buffer_spec, needs_vflip);
    needs_vflip = false;
    if (retval) {
      ALOGE("Got error code %d from DoScaling function", retval);
    }
    src_layer_spec = dst_buffer_spec;
    dest_buffer_stack.pop_back();
  }

  if (needs_rotation) {
    retval = DoRotation(src_layer_spec, dest_buffer_stack.back(), rotation,
                        needs_vflip);
    needs_vflip = false;
    if (retval) {
      ALOGE("Got error code %d from DoTransform function", retval);
    }
    src_layer_spec = dest_buffer_stack.back();
    dest_buffer_stack.pop_back();
  }

  if (needs_attenuation) {
    retval =
        DoAttenuation(src_layer_spec, dest_buffer_stack.back(), needs_vflip);
    needs_vflip = false;
    if (retval) {
      ALOGE("Got error code %d from DoBlending function", retval);
    }
    src_layer_spec = dest_buffer_stack.back();
    dest_buffer_stack.pop_back();
  }

  if (needs_copy) {
    retval = DoCopy(src_layer_spec, dest_buffer_stack.back(), needs_vflip);
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
    retval = DoBlending(src_layer_spec, dest_buffer_stack.back(), needs_vflip);
    needs_vflip = false;
    if (retval) {
      ALOGE("Got error code %d from DoBlending function", retval);
    }
    // Don't need to assign destination to source in the last one
    dest_buffer_stack.pop_back();
  }

  gralloc_module_->unlock(gralloc_module_, src_priv_handle);
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
