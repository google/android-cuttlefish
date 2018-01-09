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

#include <pthread.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>
#include <log/log.h>

#include "common/vsoc/lib/fb_bcast_region_view.h"
#include "guest/hals/gralloc/gralloc_vsoc_priv.h"

// This file contains just a skeleton hwcomposer, the first step in the
// multisided vsoc hwcomposer for cuttlefish.

using vsoc::framebuffer::FBBroadcastRegionView;

// TODO(jemoreira): FBBroadcastRegionView may belong in the HWC region

FBBroadcastRegionView* GetFBBroadcastRegionView() {
  static FBBroadcastRegionView instance;
  return &instance;
}

namespace {

// Ensures that the layer does not include any inconsistencies
int SanityCheckLayer(const hwc_layer_1& layer) {
  // Check displayFrame
  if (layer.displayFrame.left > layer.displayFrame.right ||
      layer.displayFrame.top > layer.displayFrame.bottom) {
    ALOGE(
        "%s: Malformed rectangle (displayFrame): [left = %d, right = %d, top = "
        "%d, bottom = %d]",
        __FUNCTION__, layer.displayFrame.left, layer.displayFrame.right,
        layer.displayFrame.top, layer.displayFrame.bottom);
    return -EINVAL;
  }
  // Check sourceCrop
  if (layer.sourceCrop.left > layer.sourceCrop.right ||
      layer.sourceCrop.top > layer.sourceCrop.bottom) {
    ALOGE(
        "%s: Malformed rectangle (sourceCrop): [left = %d, right = %d, top = "
        "%d, bottom = %d]",
        __FUNCTION__, layer.sourceCrop.left, layer.sourceCrop.right,
        layer.sourceCrop.top, layer.sourceCrop.bottom);
    return -EINVAL;
  }
  const vsoc_buffer_handle_t* p_handle =
      reinterpret_cast<const vsoc_buffer_handle_t*>(layer.handle);
  if (!p_handle) {
    ALOGE("Layer has a NULL buffer handle");
    return -EINVAL;
  }
  if (layer.sourceCrop.left < 0 || layer.sourceCrop.top < 0 ||
      layer.sourceCrop.right > p_handle->x_res ||
      layer.sourceCrop.bottom > p_handle->y_res) {
    ALOGE(
        "%s: Invalid sourceCrop for buffer handle: sourceCrop = [left = %d, "
        "right = %d, top = %d, bottom = %d], handle = [width = %d, height = "
        "%d]",
        __FUNCTION__, layer.sourceCrop.left, layer.sourceCrop.right,
        layer.sourceCrop.top, layer.sourceCrop.bottom, p_handle->x_res,
        p_handle->y_res);
    return -EINVAL;
  }
  return 0;
}

struct vsoc_hwc_device {
  hwc_composer_device_1_t base;
  const hwc_procs_t* procs;
  pthread_t vsync_thread;
  int64_t vsync_base_timestamp;
  int32_t vsync_period_ns;
  FBBroadcastRegionView* fb_broadcast;
  uint32_t frame_num;
};

void* vsync_thread(void* arg) {
  vsoc_hwc_device* pdev = reinterpret_cast<vsoc_hwc_device*>(arg);
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

  int64_t base_timestamp = pdev->vsync_base_timestamp;
  int64_t last_logged = base_timestamp / 1e9;
  int sent = 0;
  int last_sent = 0;
  static const int log_interval = 60;
  while (true) {
    struct timespec rt;
    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
      ALOGE("%s:%d error in vsync thread clock_gettime: %s", __FILE__, __LINE__,
            strerror(errno));
    }
    int64_t timestamp = int64_t(rt.tv_sec) * 1e9 + rt.tv_nsec;
    // Given now's timestamp calculate the time of the next vsync.
    timestamp += pdev->vsync_period_ns -
                 (timestamp - base_timestamp) % pdev->vsync_period_ns;

    rt.tv_sec = timestamp / 1e9;
    rt.tv_nsec = timestamp % static_cast<int32_t>(1e9);
    int err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &rt, NULL);
    if (err == -1) {
      ALOGE("error in vsync thread: %s", strerror(errno));
      if (errno == EINTR) {
        continue;
      }
    }

    pdev->procs->vsync(const_cast<hwc_procs_t*>(pdev->procs), 0, timestamp);
    if (rt.tv_sec - last_logged > log_interval) {
      ALOGI("Sent %d syncs in %ds", sent - last_sent, log_interval);
      last_logged = rt.tv_sec;
      last_sent = sent;
    }
    ++sent;
  }

  return NULL;
}

int hwc_prepare(struct hwc_composer_device_1* /*dev*/, size_t numDisplays,
                hwc_display_contents_1_t** displays) {
  if (!numDisplays || !displays) return 0;
  hwc_display_contents_1_t* list = displays[HWC_DISPLAY_PRIMARY];
  if (!list) return 0;

  for (size_t i = 0; i < list->numHwLayers; i++) {
    if (list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
      continue;
    }
    list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
  }

  return 0;
}

int hwc_set(struct hwc_composer_device_1* dev, size_t numDisplays,
            hwc_display_contents_1_t** displays) {
  if (!numDisplays || !displays) return 0;
  hwc_display_contents_1_t* list = displays[HWC_DISPLAY_PRIMARY];
  if (!list) return 0;
  if (!dev) {
    ALOGE("%s: dev is NULL", __FUNCTION__);
    return -EINVAL;
  }

  for (size_t i = 0; i < list->numHwLayers; i++) {
    if (vsoc_buffer_handle_t::validate(list->hwLayers[i].handle)) {
      return -EINVAL;
    }
    if (list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
      if (SanityCheckLayer(list->hwLayers[i])) {
        ALOGW("Skipping layer %zu due to failed sanity check", i);
        continue;
      }
      vsoc_hwc_device* pdev = reinterpret_cast<vsoc_hwc_device*>(dev);
      const vsoc_buffer_handle_t* fb_handle =
          reinterpret_cast<const vsoc_buffer_handle_t*>(
              list->hwLayers[i].handle);
      pdev->fb_broadcast->BroadcastNewFrame(pdev->frame_num++,
                                            fb_handle->offset);
      break;
    }
  }

  return 0;
}

int hwc_eventControl(struct hwc_composer_device_1* /*dev*/, int disp, int event,
                     int /*enabled*/) {
  if (event == HWC_EVENT_VSYNC || disp != HWC_DISPLAY_PRIMARY) {
    return 0;
  }
  return -EINVAL;
}

int hwc_blank(struct hwc_composer_device_1* /*dev*/, int disp, int /*blank*/) {
  if (disp != HWC_DISPLAY_PRIMARY) {
    return -EINVAL;
  }
  return 0;
}

int hwc_query(struct hwc_composer_device_1* dev, int what, int* value) {
  vsoc_hwc_device* pdev = reinterpret_cast<vsoc_hwc_device*>(dev);
  switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
      // we don't support the background layer
      *value = 0;
      break;
    case HWC_VSYNC_PERIOD:
      *value = pdev->vsync_period_ns;
      break;
    case HWC_DISPLAY_TYPES_SUPPORTED:
      // We only support the primary display
      *value = HWC_DISPLAY_PRIMARY_BIT;
      break;
    default:
      // unsupported query
      ALOGE("%s badness unsupported query what=%d", __FUNCTION__, what);
      return -EINVAL;
  }
  return 0;
}

void hwc_registerProcs(struct hwc_composer_device_1* dev,
                       hwc_procs_t const* procs) {
  reinterpret_cast<vsoc_hwc_device*>(dev)->procs = procs;
}

void hwc_dump(struct hwc_composer_device_1* /*dev*/, char* /*buff*/,
              int /*buff_len*/) {}

int hwc_getDisplayConfigs(struct hwc_composer_device_1* /*dev*/, int disp,
                          uint32_t* configs, size_t* numConfigs) {
  if (*numConfigs == 0) return 0;

  if (disp == HWC_DISPLAY_PRIMARY) {
    configs[0] = 0;
    *numConfigs = 1;
    return 0;
  }

  return -EINVAL;
}

int32_t vsoc_hwc_attribute(vsoc_hwc_device* pdev, uint32_t attribute) {
  switch (attribute) {
    case HWC_DISPLAY_VSYNC_PERIOD:
      return 1000000000 / pdev->fb_broadcast->refresh_rate_hz();
    case HWC_DISPLAY_WIDTH:
      return pdev->fb_broadcast->x_res();
    case HWC_DISPLAY_HEIGHT:
      return pdev->fb_broadcast->y_res();
    case HWC_DISPLAY_DPI_X:
    case HWC_DISPLAY_DPI_Y:
      // The number of pixels per thousand inches
      return pdev->fb_broadcast->dpi() * 1000;
    case HWC_DISPLAY_COLOR_TRANSFORM:
      // TODO(jemoreira): Add the other color transformations
      return HAL_COLOR_TRANSFORM_IDENTITY;
    default:
      ALOGE("unknown display attribute %u", attribute);
      return -EINVAL;
  }
}

int hwc_getDisplayAttributes(struct hwc_composer_device_1* dev, int disp,
                             uint32_t /*config*/, const uint32_t* attributes,
                             int32_t* values) {
  vsoc_hwc_device* pdev = reinterpret_cast<vsoc_hwc_device*>(dev);

  if (disp != HWC_DISPLAY_PRIMARY) {
    ALOGE("Unknown display type %u", disp);
    return -EINVAL;
  }

  for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
    values[i] = vsoc_hwc_attribute(pdev, attributes[i]);
  }

  return 0;
}

int hwc_close(hw_device_t* device) {
  vsoc_hwc_device* dev = reinterpret_cast<vsoc_hwc_device*>(device);
  pthread_kill(dev->vsync_thread, SIGTERM);
  pthread_join(dev->vsync_thread, NULL);
  delete dev;
  return 0;
}

int hwc_open(const struct hw_module_t* module, const char* name,
             struct hw_device_t** device) {
  ALOGI("Opening vsoc hwcomposer device: %s", __FUNCTION__);
  if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
    ALOGE("%s called with bad name %s", __FUNCTION__, name);
    return -EINVAL;
  }

  vsoc_hwc_device* dev = new vsoc_hwc_device();
  if (!dev) {
    ALOGE("%s failed to allocate dev", __FUNCTION__);
    return -ENOMEM;
  }
  memset(dev, 0, sizeof(*dev));

  int refreshRate = 60;
  dev->vsync_period_ns = 1000000000 / refreshRate;
  struct timespec rt;
  if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
    ALOGE("%s:%d error in clock_gettime: %s", __FILE__, __LINE__,
          strerror(errno));
  }
  dev->vsync_base_timestamp = int64_t(rt.tv_sec) * 1e9 + rt.tv_nsec;

  dev->base.common.tag = HARDWARE_DEVICE_TAG;
  dev->base.common.version = HWC_DEVICE_API_VERSION_1_1;
  dev->base.common.module = const_cast<hw_module_t*>(module);
  dev->base.common.close = hwc_close;

  dev->base.prepare = hwc_prepare;
  dev->base.set = hwc_set;
  dev->base.query = hwc_query;
  dev->base.registerProcs = hwc_registerProcs;
  dev->base.dump = hwc_dump;
  dev->base.blank = hwc_blank;
  dev->base.eventControl = hwc_eventControl;
  dev->base.getDisplayConfigs = hwc_getDisplayConfigs;
  dev->base.getDisplayAttributes = hwc_getDisplayAttributes;

  dev->fb_broadcast = GetFBBroadcastRegionView();
  if (!dev->fb_broadcast->Open()) {
    ALOGE("Unable to open framebuffer broadcaster (%s)", __FUNCTION__);
    delete dev;
    return -1;
  }

  int ret = pthread_create(&dev->vsync_thread, NULL, vsync_thread, dev);
  if (ret) {
    ALOGE("failed to start vsync thread: %s", strerror(ret));
    delete dev;
  } else {
    *device = &dev->base.common;
  }

  return -ret;
}

struct hw_module_methods_t hwc_module_methods = {hwc_open};

}  // namespace

hwc_module_t HAL_MODULE_INFO_SYM = {{HARDWARE_MODULE_TAG,
                                     HWC_MODULE_API_VERSION_0_1,
                                     HARDWARE_HAL_API_VERSION,
                                     HWC_HARDWARE_MODULE_ID,
                                     "Cuttlefish hwcomposer module",
                                     "Google",
                                     &hwc_module_methods,
                                     NULL,
                                     {0}}};
