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

#include <cutils/native_handle.h>
#include <cutils/log.h>

#include <linux/fb.h>

#include "guest/libs/legacy_framebuffer/vsoc_framebuffer.h"
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

  private_handle_t* framebuffer;
  pthread_mutex_t lock;
};

int initUserspaceFrameBuffer(struct private_module_t* module);

/*****************************************************************************/

struct priv_alloc_device_t {
  alloc_device_t  device;
  // Creates handles for the hwcomposer-specific framebuffers
  int (*alloc_hwc_framebuffer)(alloc_device_t* m,
                                buffer_handle_t* handle);
};

/*****************************************************************************/

struct private_handle_t : public native_handle {
  enum {
    PRIV_FLAGS_FRAMEBUFFER = 0x00000001
  };

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
    if (!h || h->version != sizeof(native_handle) ||
        h->numInts != sNumInts() || h->numFds != sNumFds ||
        hnd->magic != sMagic) {
      ALOGE("invalid gralloc handle (at %p)", h);
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
      out->ystride = VSoCFrameBuffer::align(width, 16);
      out->cstride = VSoCFrameBuffer::align(out->ystride / 2, 16);
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
      return (y_size + 2 * c_size + VSoCFrameBuffer::kSwiftShaderPadding);
    /*case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_RGB_565:*/
    default:
      w16 = VSoCFrameBuffer::align(w, 16);
      h16 = VSoCFrameBuffer::align(h, 16);
      return bytes_per_pixel * w16 * h16 + VSoCFrameBuffer::kSwiftShaderPadding;
  }
}

// Calculates the yoffset from a framebuffer handle. I checks the given handle
// for errors first. Returns the yoffset (non negative integer) or -1 if there
// is an error.
static inline int YOffsetFromHandle(buffer_handle_t buffer_hnd) {
    if (!buffer_hnd) {
    ALOGE("Attempt to post null buffer");
    return -1;
  }
  if (private_handle_t::validate(buffer_hnd) < 0) {
    ALOGE("Attempt to post non-vsoc handle");
    return -1;
  }
  const private_handle_t* hnd =
      reinterpret_cast<private_handle_t const*>(buffer_hnd);
  if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
    ALOGE("Attempt to post non-framebuffer");
    return -1;
  }

  const VSoCFrameBuffer& config = VSoCFrameBuffer::getInstance();
  return hnd->frame_offset / config.line_length();
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
