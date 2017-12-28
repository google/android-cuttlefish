/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <stdlib.h>

#include "guest/hals/gralloc/gralloc_vsoc_priv.h"
#include "guest/vsoc/lib/gralloc_region_view.h"

using vsoc::gralloc::GrallocRegionView;

namespace {

static const int kSwiftShaderPadding = 4;

inline void formatToYcbcr(
    int format, int width, int height, void* base_v, android_ycbcr* ycbcr) {
  uintptr_t it = reinterpret_cast<uintptr_t>(base_v);
  // Clear reserved fields;
  memset(ycbcr, 0, sizeof(*ycbcr));
  switch (format) {
    case HAL_PIXEL_FORMAT_YV12:
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
      ycbcr->ystride = align(width, 16);
      ycbcr->cstride = align(ycbcr->ystride / 2, 16);
      ycbcr->chroma_step = 1;
      ycbcr->y = reinterpret_cast<void*>(it);
      it += ycbcr->ystride * height;
      ycbcr->cr = reinterpret_cast<void*>(it);
      it += ycbcr->cstride * height / 2;
      ycbcr->cb = reinterpret_cast<void*>(it);
      break;
    default:
      ALOGE("%s: can't deal with format=0x%x", __FUNCTION__, format);
  }
}

inline int formatToBytesPerPixel(int format) {
  switch (format) {
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
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
      return 2;
    case HAL_PIXEL_FORMAT_BLOB:
      return 1;
    default:
      ALOGE("%s: unknown format=%d", __FUNCTION__, format);
      return 4;
  }
}

inline int formatToBytesPerFrame(int format, int w, int h) {
  int bytes_per_pixel = formatToBytesPerPixel(format);
  int w16, h16;
  int y_size, c_size;

  switch (format) {
    // BLOB is used to allocate buffers for JPEG formatted data. Bytes per pixel
    // is 1, the desired buffer size is in w, and h should be 1. We refrain from
    // adding additional padding, although the caller is likely to round
    // up to a page size.
    case HAL_PIXEL_FORMAT_BLOB:
      return bytes_per_pixel * w * h;
    case HAL_PIXEL_FORMAT_YV12:
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
      android_ycbcr strides;
      formatToYcbcr(format, w, h, NULL, &strides);
      y_size = strides.ystride * h;
      c_size = strides.cstride * h / 2;
      return (y_size + 2 * c_size + kSwiftShaderPadding);
    /*case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_RGB_565:*/
    default:
      w16 = align(w, 16);
      h16 = align(h, 16);
      return bytes_per_pixel * w16 * h16 + kSwiftShaderPadding;
  }
}

}

/******************************************************************************/

void dump(struct alloc_device_t */*dev*/, char */*buff*/, int /*buff_len*/) {}

/******************************************************************************/

int lock(struct gralloc_module_t const* /*module*/,
         buffer_handle_t handle,
         int /*usage*/,
         int /*l*/,
         int /*t*/,
         int /*w*/,
         int /*h*/,
         void** vaddr) {
  if (!vaddr || vsoc_buffer_handle_t::validate(handle)) {
    return -EINVAL;
  }
  // TODO(jemoreira): Check allocation usage flags against requested usage.
  const vsoc_buffer_handle_t* hnd =
      reinterpret_cast<const vsoc_buffer_handle_t*>(handle);
  void* mapped = reference_buffer(hnd);
  if (mapped == NULL) {
    ALOGE("Unable to reference buffer, %s", __FUNCTION__);
    return -1;
  }
  *vaddr = mapped;
  return 0;
}

int unlock(struct gralloc_module_t const* /*module*/, buffer_handle_t handle) {
  if (vsoc_buffer_handle_t::validate(handle)) {
    return -EINVAL;
  }
  return unreference_buffer(
      reinterpret_cast<const vsoc_buffer_handle_t*>(handle));
}

int lock_ycbcr(struct gralloc_module_t const* module,
               buffer_handle_t handle,
               int usage,
               int l,
               int t,
               int w,
               int h,
               struct android_ycbcr* ycbcr) {
  void* mapped;
  int retval = lock(module, handle, usage, l, t, w, h, &mapped);
  if (retval) {
    return retval;
  }
  const vsoc_buffer_handle_t* hnd =
      reinterpret_cast<const vsoc_buffer_handle_t*>(handle);
  formatToYcbcr(hnd->format, w, h, mapped, ycbcr);
  return 0;
}

/******************************************************************************/

static int gralloc_alloc(alloc_device_t* /*dev*/,
                         int w,
                         int h,
                         int format,
                         int /*usage*/,
                         buffer_handle_t* pHandle,
                         int* pStrideInPixels) {
  int fd = -1;

  int bytes_per_pixel = formatToBytesPerPixel(format);
  int bytes_per_line;
  int stride_in_pixels;
  int size = 0;
  uint32_t offset = 0;
  // SwiftShader can't handle RGB_888, so fail fast and hard if we try to create
  // a gralloc buffer in this format.
  ALOG_ASSERT(format != HAL_PIXEL_FORMAT_RGB_888);
  if (format == HAL_PIXEL_FORMAT_YV12) {
    bytes_per_line = align(bytes_per_pixel * w, 16);
  } else {
    bytes_per_line = align(bytes_per_pixel * w, 8);
  }
  size = align(size + formatToBytesPerFrame(format, w, h), PAGE_SIZE);
  size += PAGE_SIZE;
  fd = GrallocRegionView::GetInstance()->AllocateBuffer(size, &offset);
  if (fd < 0) {
    ALOGE("Unable to allocate buffer (%s)", strerror(-fd));
    return fd;
  }

  stride_in_pixels = bytes_per_line / bytes_per_pixel;
  vsoc_buffer_handle_t* hnd = new vsoc_buffer_handle_t(fd,
                                                       offset,
                                                       size,
                                                       format,
                                                       w, h,
                                                       stride_in_pixels);
  void* addr =
      reference_buffer(reinterpret_cast<const vsoc_buffer_handle_t*>(hnd));
  if (!addr) {
    ALOGE("Unable to reference buffer, %s", __FUNCTION__);
    return -EIO;
  }

  *pHandle = hnd;
  *pStrideInPixels = stride_in_pixels;

  return 0;
}

static int gralloc_free(alloc_device_t* /*dev*/, buffer_handle_t handle) {
  // No need to do anything else, the buffer will be atomatically deallocated
  // when the handle is closed.
  return unreference_buffer(
      reinterpret_cast<const vsoc_buffer_handle_t*>(handle));
}

static int register_buffer(struct gralloc_module_t const* /*module*/,
                          buffer_handle_t handle) {
  if (vsoc_buffer_handle_t::validate(handle)) {
    return -EINVAL;
  }
  void* addr =
      reference_buffer(reinterpret_cast<const vsoc_buffer_handle_t*>(handle));
  if (!addr) {
    ALOGE("Unable to reference buffer, %s", __FUNCTION__);
    return -EIO;
  }
  return 0;
}

int unregister_buffer(struct gralloc_module_t const* /*module*/,
                     buffer_handle_t handle) {
  if (vsoc_buffer_handle_t::validate(handle)) {
    return -EINVAL;
  }
  return unreference_buffer(
      reinterpret_cast<const vsoc_buffer_handle_t*>(handle));
}

/******************************************************************************/

static int gralloc_device_close(struct hw_device_t *dev) {
  vsoc_alloc_device_t* pdev = reinterpret_cast<vsoc_alloc_device_t*>(dev);
  if (pdev) {
    free(pdev);
  }
  return 0;
}

static int gralloc_device_open(
    const hw_module_t* module, const char* name, hw_device_t** device) {
    int status = -EINVAL;
  if (!strcmp(name, GRALLOC_HARDWARE_GPU0)) {
    vsoc_alloc_device_t *dev;
    dev = (vsoc_alloc_device_t*) malloc(sizeof(*dev));
    LOG_FATAL_IF(!dev, "%s: malloc returned NULL.", __FUNCTION__);

    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));

    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = 0; // TODO(jemoreira): Bump to 0_2 when stable
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = gralloc_device_close;

    dev->device.alloc   = gralloc_alloc;
    dev->device.free    = gralloc_free;

    if (!GrallocRegionView::GetInstance()) {
      LOG_FATAL("Unable to instantiate the gralloc region");
      free(dev);
      return -EIO;
    }

    *device = &dev->device.common;
    status = 0;
  }
  // TODO(jemoreira): Consider opening other type of devices (framebuffer)
  return status;
}

/******************************************************************************/

static struct hw_module_methods_t gralloc_module_methods = {
  .open = gralloc_device_open
};

struct vsoc_gralloc_module_t HAL_MODULE_INFO_SYM = {
  .base = {
    .common = {
      .tag = HARDWARE_MODULE_TAG,
      .version_major = GRALLOC_MODULE_API_VERSION_0_2,
      .version_minor = 0,
      .id = GRALLOC_HARDWARE_MODULE_ID,
      .name = "VSoC X86 Graphics Memory Allocator Module",
      .author = "The Android Open Source Project",
      .methods = &gralloc_module_methods,
      .dso = NULL,
      .reserved = {0},
    },
    .registerBuffer = register_buffer,
    .unregisterBuffer = unregister_buffer,
    .lock = lock,
    .unlock = unlock,
    .lock_ycbcr = lock_ycbcr,
    .perform = NULL,
  },
};
