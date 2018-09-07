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
#include <log/log.h>
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

#if defined(__ANDROID__)
#include <linux/fb.h>
#endif

#include "gralloc_vsoc_priv.h"
#include "region_registry.h"

#include "common/libs/auto_resources/auto_resources.h"
#include "common/libs/threads/cuttlefish_thread.h"
#include "common/vsoc/lib/screen_region_view.h"

using vsoc::screen::ScreenRegionView;

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

static int fb_post(struct framebuffer_device_t* dev __unused,
                   buffer_handle_t buffer_handle) {
  static int frame_buffer_idx = 0;

  auto screen_view = ScreenRegionView::GetInstance();

  void* frame_buffer = screen_view->GetBuffer(frame_buffer_idx);
  const private_handle_t* p_handle =
      reinterpret_cast<const private_handle_t*>(buffer_handle);
  void* buffer;
  int retval =
      reinterpret_cast<gralloc_module_t*>(dev->common.module)
          ->lock(reinterpret_cast<const gralloc_module_t*>(dev->common.module),
                 buffer_handle, GRALLOC_USAGE_SW_READ_OFTEN, 0, 0,
                 p_handle->x_res, p_handle->y_res, &buffer);
  if (retval != 0) {
    ALOGE("Got error code %d from lock function", retval);
    return -1;
  }
  memcpy(frame_buffer, buffer, screen_view->buffer_size());
  screen_view->BroadcastNewFrame(frame_buffer_idx);

  frame_buffer_idx = (frame_buffer_idx + 1) % screen_view->number_of_buffers();

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
  if (strcmp(name, GRALLOC_HARDWARE_FB0) != 0) {
    return -EINVAL;
  }
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

  auto screen_view = ScreenRegionView::GetInstance();

  int stride =
    screen_view->line_length() / screen_view->bytes_per_pixel();
  int format = HAL_PIXEL_FORMAT_RGBX_8888;
  const_cast<uint32_t&>(dev->device.flags) = 0;
  const_cast<uint32_t&>(dev->device.width) = screen_view->x_res();
  const_cast<uint32_t&>(dev->device.height) = screen_view->y_res();
  const_cast<int&>(dev->device.stride) = stride;
  const_cast<int&>(dev->device.format) = format;
  const_cast<float&>(dev->device.xdpi) = screen_view->dpi();
  const_cast<float&>(dev->device.ydpi) = screen_view->dpi();
  const_cast<float&>(dev->device.fps) =
    (screen_view->refresh_rate_hz() * 1000) / 1000.0f;
  const_cast<int&>(dev->device.minSwapInterval) = 1;
  const_cast<int&>(dev->device.maxSwapInterval) = 1;
  *device = &dev->device.common;

  return 0;
}
