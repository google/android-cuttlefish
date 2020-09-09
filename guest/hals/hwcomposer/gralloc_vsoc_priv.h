#pragma once
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
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/mman.h>
#include <hardware/gralloc.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <cutils/native_handle.h>
#include <log/log.h>

namespace cuttlefish {
namespace screen {

struct ScreenRegionView {
  static int align(int input) {
    auto constexpr alignment = 16;
    return (input + alignment - 1) & -alignment;
  }
  static constexpr int kSwiftShaderPadding = 4;
};

}
}

struct private_module_t {
  gralloc_module_t base;
};

struct private_handle_t : public native_handle {
  // file-descriptors
  int     fd;
  // ints
  int     magic;
  int     flags;
  int     format;
  int     x_res;
  int     y_res;
  int     stride_in_pixels;
  // Use to indicate which frame we're using.
  int     frame_offset;
  int     total_size;
  int     lock_level;
};


static inline int formatToBytesPerPixel(int format) {
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_FP16:
      return 8;
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    // The camera 3.0 implementation assumes that IMPLEMENTATION_DEFINED
    // means HAL_PIXEL_FORMAT_RGBA_8888
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
      return 4;
    case HAL_PIXEL_FORMAT_RGB_888:
      return 3;
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_YV12:
#ifdef GRALLOC_MODULE_API_VERSION_0_2
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
#endif
      return 2;
    case HAL_PIXEL_FORMAT_BLOB:
      return 1;
    default:
      ALOGE("%s: unknown format=%d", __FUNCTION__, format);
      return 8;
  }
}

inline const char* pixel_format_to_string(int format) {
  switch (format) {
    // Formats that are universal across versions
    case HAL_PIXEL_FORMAT_RGBA_8888:
      return "RGBA_8888";
    case HAL_PIXEL_FORMAT_RGBX_8888:
      return "RGBX_8888";
    case HAL_PIXEL_FORMAT_BGRA_8888:
      return "BGRA_8888";
    case HAL_PIXEL_FORMAT_RGB_888:
      return "RGB_888";
    case HAL_PIXEL_FORMAT_RGB_565:
      return "RGB_565";
    case HAL_PIXEL_FORMAT_YV12:
      return "YV12";
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      return "YCrCb_420_SP";
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
      return "YCbCr_422_SP";
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
      return "YCbCr_422_I";

    // First supported on JBMR1 (API 17)
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
      return "IMPLEMENTATION_DEFINED";
    case HAL_PIXEL_FORMAT_BLOB:
      return "BLOB";
    // First supported on JBMR2 (API 18)
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
      return "YCbCr_420_888";
    case HAL_PIXEL_FORMAT_Y8:
      return "Y8";
    case HAL_PIXEL_FORMAT_Y16:
      return "Y16";
    // Support was added in L (API 21)
    case HAL_PIXEL_FORMAT_RAW_OPAQUE:
      return "RAW_OPAQUE";
    // This is an alias for RAW_SENSOR in L and replaces it in M.
    case HAL_PIXEL_FORMAT_RAW16:
      return "RAW16";
    case HAL_PIXEL_FORMAT_RAW10:
      return "RAW10";
    case HAL_PIXEL_FORMAT_YCbCr_444_888:
      return "YCbCr_444_888";
    case HAL_PIXEL_FORMAT_YCbCr_422_888:
      return "YCbCr_422_888";
    case HAL_PIXEL_FORMAT_RAW12:
      return "RAW12";
    case HAL_PIXEL_FORMAT_FLEX_RGBA_8888:
      return "FLEX_RGBA_8888";
    case HAL_PIXEL_FORMAT_FLEX_RGB_888:
      return "FLEX_RGB_888";
    case HAL_PIXEL_FORMAT_RGBA_FP16:
      return "RGBA_FP16";
  }
  return "UNKNOWN";
}


static inline void formatToYcbcr(
    int format, int width, int height, void* base_v, android_ycbcr* out) {
  char* it = static_cast<char*>(base_v);
  // Clear reserved fields;
  memset(out, 0, sizeof(*out));
  switch (format) {
    case HAL_PIXEL_FORMAT_YV12:
#ifdef GRALLOC_MODULE_API_VERSION_0_2
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
#endif
      out->ystride = cuttlefish::screen::ScreenRegionView::align(width);
      out->cstride =
          cuttlefish::screen::ScreenRegionView::align(out->ystride / 2);
      out->chroma_step = 1;
      out->y = it;
      it += out->ystride * height;
      out->cr = it;
      it += out->cstride * height / 2;
      out->cb = it;
      break;
    default:
      ALOGE("%s: can't deal with format=0x%x (%s)",
            __FUNCTION__, format, pixel_format_to_string(format));
  }
}
