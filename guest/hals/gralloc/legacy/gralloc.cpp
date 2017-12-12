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
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <utils/String8.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <guest/libs/platform_support/api_level_fixes.h>

#include "guest/libs/legacy_framebuffer/vsoc_framebuffer.h"
#include "guest/libs/legacy_framebuffer/vsoc_framebuffer_control.h"
#include "guest/libs/legacy_framebuffer/RegionRegistry.h"
#include "gralloc_vsoc_priv.h"
#include "common/libs/auto_resources/auto_resources.h"

/*****************************************************************************/

static int gralloc_alloc_buffer(
    alloc_device_t* /*dev*/, int format, int w, int h,
    buffer_handle_t* pHandle, int* pStrideInPixels) {
  int err = 0;
  int fd = -1;
  static int sequence = 0;

  int bytes_per_pixel = formatToBytesPerPixel(format);
  int bytes_per_line;
  int stride_in_pixels;
  int size = 0;
  // SwiftShader can't handle RGB_888, so fail fast and hard if we try to create
  // a gralloc buffer in this format.
  ALOG_ASSERT(format != HAL_PIXEL_FORMAT_RGB_888);
  if (format == HAL_PIXEL_FORMAT_YV12) {
    bytes_per_line = VSoCFrameBuffer::align(bytes_per_pixel * w, 16);
  } else {
    bytes_per_line = VSoCFrameBuffer::align(bytes_per_pixel * w);
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

static int ensure_framebuffer_allocated(private_module_t* m) {
  // allocate the framebuffer
  if (m->framebuffer == NULL) {
    // The framebuffer is mapped once and forever.
    int err = initUserspaceFrameBuffer(m);
    if (err < 0) {
      ALOGE("Failed to map framebuffer (%d)", errno);
      return err;
    }
  }
  return 0;
}

static int gralloc_free_framebuffer(private_handle_t const * hnd) {
    static const VSoCFrameBuffer& config = VSoCFrameBuffer::getInstance();
    static VSoCFrameBufferControl& fb_control =
        VSoCFrameBufferControl::getInstance();

    // free this buffer
    const size_t bufferSize = config.line_length() * config.y_res();
    int index = hnd->frame_offset / bufferSize;
    return fb_control.UnsetBufferBits(1LU << index);
}

// Allocates a framebuffer taken from the set of buffers given by buffer_mask.
static int gralloc_alloc_framebuffer(alloc_device_t* dev,
                                     buffer_handle_t* pHandle,
                                     int* pStrideInPixels,
                                     uint32_t buffer_mask) {
  static const VSoCFrameBuffer& config = VSoCFrameBuffer::getInstance();
  static VSoCFrameBufferControl& fb_control =
      VSoCFrameBufferControl::getInstance();

  private_module_t* m = reinterpret_cast<private_module_t*>(
      dev->common.module);

  ensure_framebuffer_allocated(m);

  const uint32_t numBuffers = VSoCFrameBuffer::kNumBuffers;
  const size_t bufferSize = config.bufferSize();

  // Paranoia: Force the mask to be valid
  buffer_mask &= (1LU << numBuffers) - 1;

  uint32_t bit = fb_control.GetAndSetNextAvailableBufferBit(buffer_mask);
  if (!bit) {
    // All buffers in the mask have been allocated already or there was another
    // error
    return -ENOMEM;
  }

  int frame_offset = 0;
  while (bit != 1LU) {
    bit >>= 1;
    frame_offset += bufferSize;
  }

  int stride_in_pixels = config.line_length() / (config.bits_per_pixel() / 8);

  // create a "fake" handle for it
  private_handle_t* hnd = new private_handle_t(
      dup(m->framebuffer->fd), config.total_buffer_size(),
      config.hal_format(), config.x_res(), config.y_res(),
      stride_in_pixels, private_handle_t::PRIV_FLAGS_FRAMEBUFFER, frame_offset);

  *pHandle = hnd;
  if (pStrideInPixels) {
    *pStrideInPixels = stride_in_pixels;
  }

  return 0;
}

static int gralloc_alloc_sf_framebuffer(alloc_device_t* dev,
                                        buffer_handle_t* pHandle,
                                        int* pStrideInPixels) {
  uint32_t mask = (1LU << VSoCFrameBuffer::kNumSfBuffers) - 1LU;
  // Skip the first buffers since those are for HWC usage
  mask <<= VSoCFrameBuffer::kNumHwcBuffers;
  return gralloc_alloc_framebuffer(dev, pHandle, pStrideInPixels, mask);
}

static int gralloc_alloc_hwc_framebuffer(alloc_device_t* dev,
                                         buffer_handle_t* pHandle) {
  // Use the first kNumHwcBuffers for hwcomposer
  uint32_t mask = (1LU << VSoCFrameBuffer::kNumHwcBuffers) - 1LU;
  return gralloc_alloc_framebuffer(dev, pHandle, NULL, mask);
}

/*****************************************************************************/

static int gralloc_alloc(
    alloc_device_t* dev, int w, int h, int format, int usage,
    buffer_handle_t* pHandle, int* pStrideInPixels) {
  if (!pHandle || !pStrideInPixels)
    return -EINVAL;

  int err;
  if (usage & GRALLOC_USAGE_HW_FB) {
    err = gralloc_alloc_sf_framebuffer(dev, pHandle, pStrideInPixels);
  } else {
    err = gralloc_alloc_buffer(dev, format, w, h, pHandle, pStrideInPixels);
  }

  if (err < 0) {
    return err;
  }
  return 0;
}

static int gralloc_free(alloc_device_t* /*dev*/, buffer_handle_t handle) {
  if (private_handle_t::validate(handle) < 0) {
    return -EINVAL;
  }

  int retval = 0;

  private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(
    handle);
  if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
    retval = gralloc_free_framebuffer(hnd);
  } else {
    retval = unreference_region(__FUNCTION__, hnd);
  }

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
    dev->alloc_hwc_framebuffer = gralloc_alloc_hwc_framebuffer;


    *device = &dev->device.common;
    status = 0;
  } else {
    status = fb_device_open(module, name, device);
  }
  return status;
}

/*****************************************************************************/

static struct hw_module_methods_t gralloc_module_methods = {
  VSOC_STATIC_INITIALIZER(open) gralloc_device_open
};

struct private_module_t HAL_MODULE_INFO_SYM = {
  VSOC_STATIC_INITIALIZER(base) {
    VSOC_STATIC_INITIALIZER(common) {
      VSOC_STATIC_INITIALIZER(tag) HARDWARE_MODULE_TAG,
#ifdef GRALLOC_MODULE_API_VERSION_0_2
      VSOC_STATIC_INITIALIZER(version_major) GRALLOC_MODULE_API_VERSION_0_2,
#else
      VSOC_STATIC_INITIALIZER(version_major) 1,
#endif
      VSOC_STATIC_INITIALIZER(version_minor) 0,
      VSOC_STATIC_INITIALIZER(id) GRALLOC_HARDWARE_MODULE_ID,
      VSOC_STATIC_INITIALIZER(name) "VSOC X86 Graphics Memory Allocator Module",
      VSOC_STATIC_INITIALIZER(author) "The Android Open Source Project",
      VSOC_STATIC_INITIALIZER(methods) &gralloc_module_methods,
      VSOC_STATIC_INITIALIZER(dso) NULL,
      VSOC_STATIC_INITIALIZER(reserved) {0},
    },
    VSOC_STATIC_INITIALIZER(registerBuffer) gralloc_register_buffer,
    VSOC_STATIC_INITIALIZER(unregisterBuffer) gralloc_unregister_buffer,
    VSOC_STATIC_INITIALIZER(lock) gralloc_lock,
    VSOC_STATIC_INITIALIZER(unlock) gralloc_unlock,
#ifdef GRALLOC_MODULE_API_VERSION_0_2
    VSOC_STATIC_INITIALIZER(perform) NULL,
    VSOC_STATIC_INITIALIZER(lock_ycbcr) gralloc_lock_ycbcr,
#endif
  },
  VSOC_STATIC_INITIALIZER(framebuffer) 0,
  VSOC_STATIC_INITIALIZER(lock) PTHREAD_MUTEX_INITIALIZER,
};
