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
#include <atomic>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <cutils/ashmem.h>
#include <log/log.h>
#include <cutils/atomic.h>
#include <utils/String8.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include "gralloc_vsoc_priv.h"
#include "region_registry.h"

using cuttlefish::screen::ScreenRegionView;

/*****************************************************************************/

static inline size_t roundUpToPageSize(size_t x) {
  return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

static int gralloc_alloc_buffer(
    alloc_device_t* /*dev*/, int format, int w, int h,
    buffer_handle_t* pHandle, int* pStrideInPixels) {
  int err = 0;
  int fd = -1;
  static std::atomic<int> sequence;

  int bytes_per_pixel = formatToBytesPerPixel(format);
  int bytes_per_line;
  int stride_in_pixels;
  int size = 0;
  // SwiftShader can't handle RGB_888, so fail fast and hard if we try to create
  // a gralloc buffer in this format.
  ALOG_ASSERT(format != HAL_PIXEL_FORMAT_RGB_888);
  if (format == HAL_PIXEL_FORMAT_YV12) {
    bytes_per_line = ScreenRegionView::align(bytes_per_pixel * w);
  } else {
    bytes_per_line = ScreenRegionView::align(bytes_per_pixel * w);
  }
  size = roundUpToPageSize(size + formatToBytesPerFrame(format, w, h));
  size += PAGE_SIZE;
  fd = ashmem_create_region(
      android::String8::format(
          "gralloc-%d.%d", getpid(), sequence++).string(),
      size);
  if (fd < 0) {
    ALOGE("couldn't create ashmem (%s)", strerror(-errno));
    err = -errno;
  }

  if (err == 0) {
    stride_in_pixels = bytes_per_line / bytes_per_pixel;
    private_handle_t* hnd =
        new private_handle_t(fd, size, format, w, h, stride_in_pixels, 0);
    void* base = reference_region(__FUNCTION__, hnd);
    if (base) {
      *pHandle = hnd;
      *pStrideInPixels = stride_in_pixels;
    } else {
      err = -EIO;
    }
  }

  ALOGE_IF(err, "gralloc failed err=%s", strerror(-err));

  return err;
}

/*****************************************************************************/

static int gralloc_alloc(alloc_device_t* dev, int w, int h, int format,
                         int /*usage*/, buffer_handle_t* pHandle,
                         int* pStrideInPixels) {
  if (!pHandle || !pStrideInPixels)
    return -EINVAL;

  int err = gralloc_alloc_buffer(dev, format, w, h, pHandle, pStrideInPixels);

  if (err < 0) {
    return err;
  }
  return 0;
}

static int gralloc_free(alloc_device_t* /*dev*/, buffer_handle_t handle) {
  if (private_handle_t::validate(handle) < 0) {
    return -EINVAL;
  }

  private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(
    handle);
  int retval = unreference_region(__FUNCTION__, hnd);

  close(hnd->fd);
  delete hnd;
  return retval;
}

/*****************************************************************************/

static int gralloc_close(struct hw_device_t *dev) {
  priv_alloc_device_t* ctx = reinterpret_cast<priv_alloc_device_t*>(dev);
  if (ctx) {
    /* TODO: keep a list of all buffer_handle_t created, and free them
     * all here.
     */
    free(ctx);
  }
  return 0;
}

static int gralloc_device_open(
    const hw_module_t* module, const char* name, hw_device_t** device) {
  int status = -EINVAL;
  if (!strcmp(name, GRALLOC_HARDWARE_GPU0)) {
    priv_alloc_device_t *dev;
    dev = (priv_alloc_device_t*) malloc(sizeof(*dev));
    LOG_FATAL_IF(!dev, "%s: malloc returned NULL.", __FUNCTION__);

    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));

    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = 0;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = gralloc_close;

    dev->device.alloc   = gralloc_alloc;
    dev->device.free    = gralloc_free;

    *device = &dev->device.common;
    status = 0;
  } else {
    ALOGE("Need to create framebuffer, but it is unsupported");
  }
  return status;
}

/*****************************************************************************/

static struct hw_module_methods_t gralloc_module_methods = {
  .open = gralloc_device_open
};

struct private_module_t HAL_MODULE_INFO_SYM = {
  .base = {
    .common = {
      .tag = HARDWARE_MODULE_TAG,
#ifdef GRALLOC_MODULE_API_VERSION_0_2
      .version_major = GRALLOC_MODULE_API_VERSION_0_2,
#else
      .version_major = 1,
#endif
      .version_minor = 0,
      .id = GRALLOC_HARDWARE_MODULE_ID,
      .name = "VSOC X86 Graphics Memory Allocator Module",
      .author = "The Android Open Source Project",
      .methods = &gralloc_module_methods,
      .dso = NULL,
      .reserved = {0},
    },
    .registerBuffer = gralloc_register_buffer,
    .unregisterBuffer = gralloc_unregister_buffer,
    .lock = gralloc_lock,
    .unlock = gralloc_unlock,
#ifdef GRALLOC_MODULE_API_VERSION_0_2
    .perform = NULL,
    .lock_ycbcr = gralloc_lock_ycbcr,
#endif
    .getTransportSize = gralloc_get_transport_size,
    .validateBufferSize = gralloc_validate_buffer_size,
  },
};
