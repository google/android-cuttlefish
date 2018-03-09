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
#include <sys/cdefs.h>
#include <sys/mman.h>
#include <hardware/gralloc.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <cutils/native_handle.h>
#include <cutils/log.h>

#include <linux/fb.h>

#include "common/vsoc/lib/screen_region_view.h"
#include "guest/libs/platform_support/api_level_fixes.h"

#ifndef GRALLOC_MODULE_API_VERSION_0_2
// This structure will be defined in later releases of Android. Declare it
// here to allow us to structure the code well.
struct android_ycbcr {
  void* y;
  void* cb;
  void* cr;
  size_t ystride;
  size_t cstride;
  size_t chroma_step;
  uint32_t reserved[8];
};
#endif

/*****************************************************************************/

struct private_handle_t;

struct private_module_t {
  gralloc_module_t base;
};

/*****************************************************************************/

struct priv_alloc_device_t {
  alloc_device_t  device;
};

/*****************************************************************************/

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

  static inline int sNumInts() {
    return (((sizeof(private_handle_t) - sizeof(native_handle_t))/sizeof(int)) - sNumFds);
  }
  static const int sNumFds = 1;
  static const int sMagic = 0x3141592;

  private_handle_t(int fd, int size, int format, int x_res, int y_res,
                   int stride_in_pixels, int flags, int frame_offset = 0)
      : fd(fd),
        magic(sMagic),
        flags(flags),
        format(format),
        x_res(x_res),
        y_res(y_res),
        stride_in_pixels(stride_in_pixels),
        frame_offset(frame_offset),
        total_size(size),
        lock_level(0) {
    version = sizeof(native_handle);
    numInts = sNumInts();
    numFds = sNumFds;
  }

  ~private_handle_t() {
    magic = 0;
  }

  static int validate(const native_handle* h) {
    const private_handle_t* hnd = (const private_handle_t*)h;
    if (!h) {
      ALOGE("invalid gralloc handle (at %p): NULL pointer", h);
      return -EINVAL;
    }
    if (h->version != sizeof(native_handle)) {
      ALOGE(
          "invalid gralloc handle (at %p): Wrong version(observed: %d, "
          "expected: %zu)",
          h,
          h->version,
          sizeof(native_handle));
      return -EINVAL;
    }
    if (h->numInts != sNumInts()) {
      ALOGE(
          "invalid gralloc handle (at %p): Wrong number of ints(observed: %d, "
          "expected: %d)",
          h,
          h->numInts,
          sNumInts());
      return -EINVAL;
    }
    if (h->numFds != sNumFds) {
      ALOGE(
          "invalid gralloc handle (at %p): Wrong number of file "
          "descriptors(observed: %d, expected: %d)",
          h,
          h->numFds,
          sNumFds);
      return -EINVAL;
    }
    if (hnd->magic != sMagic) {
      ALOGE(
          "invalid gralloc handle (at %p): Wrong magic number(observed: %d, "
          "expected: %d)",
          h,
          hnd->magic,
          sMagic);
      return -EINVAL;
    }
    return 0;
  }
};


static inline int formatToBytesPerPixel(int format) {
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
#if VSOC_PLATFORM_SDK_AFTER(J)
    // The camera 3.0 implementation assumes that IMPLEMENTATION_DEFINED
    // means HAL_PIXEL_FORMAT_RGBA_8888
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
#endif
      return 4;
    case HAL_PIXEL_FORMAT_RGB_888:
      return 3;
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_YV12:
#ifdef GRALLOC_MODULE_API_VERSION_0_2
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
#endif
      return 2;
#if VSOC_PLATFORM_SDK_AFTER(J)
    case HAL_PIXEL_FORMAT_BLOB:
      return 1;
#endif
    default:
      ALOGE("%s: unknown format=%d", __FUNCTION__, format);
      return 4;
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

#if VSOC_PLATFORM_SDK_AFTER(J)
    // First supported on JBMR1 (API 17)
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
      return "IMPLEMENTATION_DEFINED";
    case HAL_PIXEL_FORMAT_BLOB:
      return "BLOB";
#endif
#if VSOC_PLATFORM_SDK_AFTER(J_MR1)
    // First supported on JBMR2 (API 18)
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
      return "YCbCr_420_888";
    case HAL_PIXEL_FORMAT_Y8:
      return "Y8";
    case HAL_PIXEL_FORMAT_Y16:
      return "Y16";
#endif
#if VSOC_PLATFORM_SDK_AFTER(K)
    // Support was added in L (API 21)
    case HAL_PIXEL_FORMAT_RAW_OPAQUE:
      return "RAW_OPAQUE";
    // This is an alias for RAW_SENSOR in L and replaces it in M.
    case HAL_PIXEL_FORMAT_RAW16:
      return "RAW16";
    case HAL_PIXEL_FORMAT_RAW10:
      return "RAW10";
#endif
#if VSOC_PLATFORM_SDK_AFTER(L_MR1)
    case HAL_PIXEL_FORMAT_RAW12:
      return "RAW12";
#endif

      // Formats that have been removed
#if VSOC_PLATFORM_SDK_BEFORE(K)
    // Support was dropped on K (API 19)
    case HAL_PIXEL_FORMAT_RGBA_5551:
      return "RGBA_5551";
    case HAL_PIXEL_FORMAT_RGBA_4444:
      return "RGBA_4444";
#endif
#if VSOC_PLATFORM_SDK_BEFORE(L)
    // Renamed to RAW_16 in L. Both were present for L, but it was completely
    // removed in M.
    case HAL_PIXEL_FORMAT_RAW_SENSOR:
      return "RAW_SENSOR";
#endif
#if VSOC_PLATFORM_SDK_AFTER(J_MR2) && VSOC_PLATFORM_SDK_BEFORE(M)
    // Supported K, L, and LMR1. Not supported on JBMR0, JBMR1, JBMR2, and M
    case HAL_PIXEL_FORMAT_sRGB_X_8888:
      return "sRGB_X_8888";
    case HAL_PIXEL_FORMAT_sRGB_A_8888:
      return "sRGB_A_8888";
#endif
#if VSOC_PLATFORM_SDK_AFTER(L_MR1) && VSOC_PLATFORM_SDK_BEFORE(P)
    // Added in LMR1. Hidden from HALs since P.
    case HAL_PIXEL_FORMAT_YCbCr_444_888:
      return "YCbCr_444_888";
    case HAL_PIXEL_FORMAT_YCbCr_422_888:
      return "YCbCr_422_888";
    case HAL_PIXEL_FORMAT_FLEX_RGBA_8888:
      return "FLEX_RGBA_8888";
    case HAL_PIXEL_FORMAT_FLEX_RGB_888:
      return "FLEX_RGB_888";
#endif
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
      out->ystride = vsoc::screen::ScreenRegionView::align(width, 16);
      out->cstride =
          vsoc::screen::ScreenRegionView::align(out->ystride / 2, 16);
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

static inline int formatToBytesPerFrame(int format, int w, int h) {
  int bytes_per_pixel = formatToBytesPerPixel(format);
  int w16, h16;
  int y_size, c_size;

  switch (format) {
#if VSOC_PLATFORM_SDK_AFTER(J)
    // BLOB is used to allocate buffers for JPEG formatted data. Bytes per pixel
    // is 1, the desired buffer size is in w, and h should be 1. We refrain from
    // adding additional padding, although the caller is likely to round
    // up to a page size.
    case HAL_PIXEL_FORMAT_BLOB:
      return bytes_per_pixel * w * h;
#endif
    case HAL_PIXEL_FORMAT_YV12:
#ifdef GRALLOC_MODULE_API_VERSION_0_2
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
#endif
      android_ycbcr strides;
      formatToYcbcr(format, w, h, NULL, &strides);
      y_size = strides.ystride * h;
      c_size = strides.cstride * h / 2;
      return (y_size + 2 * c_size +
              vsoc::screen::ScreenRegionView::kSwiftShaderPadding);
    /*case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_RGB_565:*/
    default:
      w16 = vsoc::screen::ScreenRegionView::align(w, 16);
      h16 = vsoc::screen::ScreenRegionView::align(h, 16);
      return bytes_per_pixel * w16 * h16 +
             vsoc::screen::ScreenRegionView::kSwiftShaderPadding;
  }
}

int fb_device_open(
    const hw_module_t* module, const char* name, hw_device_t** device);

int gralloc_lock(
    gralloc_module_t const* module,
    buffer_handle_t handle, int usage,
    int l, int t, int w, int h,
    void** vaddr);

int gralloc_unlock(
    gralloc_module_t const* module, buffer_handle_t handle);

int gralloc_register_buffer(
    gralloc_module_t const* module, buffer_handle_t handle);

int gralloc_unregister_buffer(
    gralloc_module_t const* module, buffer_handle_t handle);

int gralloc_lock_ycbcr(
    struct gralloc_module_t const* module,
    buffer_handle_t handle, int usage,
    int l, int t, int w, int h,
    struct android_ycbcr *ycbcr);
