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
#include <sys/mman.h>

#include <dlfcn.h>

#include <cutils/ashmem.h>
#include <cutils/log.h>
#include <cutils/properties.h>

#include <sys/system_properties.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>

#include <cutils/atomic.h>
#include <private/android_filesystem_config.h>

#if defined(__ANDROID__)
#include <linux/fb.h>
#endif

#include "gralloc_gce_priv.h"

#include <GceFrameBuffer.h>
#include <GceFrameBufferControl.h>
#include <RegionRegistry.h>
#include "AutoResources.h"

/*****************************************************************************/

struct fb_context_t {
  framebuffer_device_t  device;
};

/*****************************************************************************/

static int fb_setSwapInterval(struct framebuffer_device_t* dev, int interval) {
  if (interval < dev->minSwapInterval || interval > dev->maxSwapInterval) {
    return -EINVAL;
  }
  // FIXME: implement fb_setSwapInterval
  return 0;
}

/*
 * These functions (and probably the entire framebuffer device) are most likely
 * not used when the hardware composer device is present, however is hard to be
 * 100% sure.
 */
static int fb_setUpdateRect(
    struct framebuffer_device_t* dev __unused, int l, int t, int w, int h) {
  if (((w|h) <= 0) || ((l|t)<0)) {
    return -EINVAL;
  }
  // TODO(jemoreira): Find a way to broadcast this with the framebuffer control.
  return 0;
}

static int fb_post(struct framebuffer_device_t* dev __unused, buffer_handle_t buffer) {
  const int yoffset = YOffsetFromHandle(buffer);
  if (yoffset >= 0) {
    int retval =
        GceFrameBufferControl::getInstance().BroadcastFrameBufferChanged(
            yoffset);
    if (retval) ALOGI("Failed to post framebuffer");

    return retval;
  }
  return -1;
}

/*****************************************************************************/

int initUserspaceFrameBuffer(struct private_module_t* module) {
  avd::LockGuard<pthread_mutex_t> guard(module->lock);
  if (module->framebuffer) {
    return 0;
  }

  int fd;
  if (!GceFrameBuffer::OpenFrameBuffer(&fd)) {
    return -errno;
  }

  const GceFrameBuffer& config = GceFrameBuffer::getInstance();

  /*
   * map the framebuffer
   */
  module->framebuffer =
      new private_handle_t(fd,
                           config.total_buffer_size(),
                           config.hal_format(),
                           config.x_res(),
                           config.y_res(),
                           config.line_length() / (config.bits_per_pixel() / 8),
                           private_handle_t::PRIV_FLAGS_FRAMEBUFFER);
  reference_region("framebuffer_init", module->framebuffer);

  return 0;
}


/*****************************************************************************/

static int fb_close(struct hw_device_t *dev) {
  fb_context_t* ctx = (fb_context_t*)dev;
  if (ctx) {
    free(ctx);
  }
  return 0;
}

int fb_device_open(
    hw_module_t const* module, const char* name, hw_device_t** device) {
  int status = -EINVAL;
  if (!strcmp(name, GRALLOC_HARDWARE_FB0)) {
    /* initialize our state here */
    fb_context_t* dev = (fb_context_t*) malloc(sizeof(*dev));
    LOG_FATAL_IF(!dev, "%s: malloc returned NULL.", __FUNCTION__);
    memset(dev, 0, sizeof(*dev));

    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = 0;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = fb_close;
    dev->device.setSwapInterval = fb_setSwapInterval;
    dev->device.post            = fb_post;
    dev->device.setUpdateRect   = fb_setUpdateRect;

    private_module_t* m = (private_module_t*)module;

    status = initUserspaceFrameBuffer(m);

    const GceFrameBuffer& config = GceFrameBuffer::getInstance();

    if (status >= 0) {
      int stride = config.line_length() / (config.bits_per_pixel() / 8);
      int format = config.hal_format();
      const_cast<uint32_t&>(dev->device.flags) = 0;
      const_cast<uint32_t&>(dev->device.width) = config.x_res();
      const_cast<uint32_t&>(dev->device.height) = config.y_res();
      const_cast<int&>(dev->device.stride) = stride;
      const_cast<int&>(dev->device.format) = format;
      const_cast<float&>(dev->device.xdpi) = config.dpi();
      const_cast<float&>(dev->device.ydpi) = config.dpi();
      // TODO (jemoreira): DRY!! Managed by the vsync thread in the hwcomposer
      const_cast<float&>(dev->device.fps) = (60 * 1000) / 1000.0f;
      const_cast<int&>(dev->device.minSwapInterval) = 1;
      const_cast<int&>(dev->device.maxSwapInterval) = 1;
      *device = &dev->device.common;
    }
  }
  return status;
}
