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

// Versions of hwcomposer we implement:
// JB: 0.3
// JB-MR1 to N : 1.1
// N-MR1 to ... : We report 1.1 but SurfaceFlinger has the option to use an
// adapter to treat our 1.1 hwcomposer as a 2.0. If SF stops using that adapter
// to support 1.1 implementations it can be copied into cuttlefish from
// frameworks/native/services/surfaceflinger/DisplayHardware/HWC2On1Adapter.*

#include <guest/libs/platform_support/api_level_fixes.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>

#define HWC_REMOVE_DEPRECATED_VERSIONS 1

#include <cutils/compiler.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>
#include <utils/String8.h>
#include <utils/Vector.h>

#include "common/vsoc/lib/screen_region_view.h"
#include "guest/hals/gralloc/legacy/gralloc_vsoc_priv.h"
#include <sync/sync.h>

#include "base_composer.h"
#include "geometry_utils.h"
#include "hwcomposer_common.h"
#include "stats_keeper.h"
#include "vsoc_composer.h"

using vsoc::screen::ScreenRegionView;

#ifdef USE_OLD_HWCOMPOSER
typedef cvd::BaseComposer InnerComposerType;
#else
typedef cvd::VSoCComposer InnerComposerType;
#endif

#ifdef GATHER_STATS
typedef cvd::StatsKeepingComposer<InnerComposerType> ComposerType;
#else
typedef InnerComposerType ComposerType;
#endif

struct vsoc_hwc_composer_device_1_t {
  vsoc_hwc_device base;
  const hwc_procs_t* procs;
  pthread_t vsync_thread;
  int64_t vsync_base_timestamp;
  int32_t vsync_period_ns;
  ComposerType* composer;
};

namespace {

// Ensures that the layer does not include any inconsistencies
bool IsValidLayer(const vsoc_hwc_layer& layer) {
  if (layer.flags & HWC_SKIP_LAYER) {
    // A layer we are asked to skip is valid regardless of its contents
    return true;
  }
  // Check displayFrame
  if (layer.displayFrame.left > layer.displayFrame.right ||
      layer.displayFrame.top > layer.displayFrame.bottom) {
    ALOGE(
        "%s: Malformed rectangle (displayFrame): [left = %d, right = %d, top = "
        "%d, bottom = %d]",
        __FUNCTION__, layer.displayFrame.left, layer.displayFrame.right,
        layer.displayFrame.top, layer.displayFrame.bottom);
    return false;
  }
  // Validate the handle
  if (private_handle_t::validate(layer.handle) != 0) {
    ALOGE("%s: Layer contains an invalid gralloc handle.", __FUNCTION__);
    return false;
  }
  const private_handle_t* p_handle =
      reinterpret_cast<const private_handle_t*>(layer.handle);
  // Check sourceCrop
  if (layer.sourceCrop.left > layer.sourceCrop.right ||
      layer.sourceCrop.top > layer.sourceCrop.bottom) {
    ALOGE(
        "%s: Malformed rectangle (sourceCrop): [left = %d, right = %d, top = "
        "%d, bottom = %d]",
        __FUNCTION__, layer.sourceCrop.left, layer.sourceCrop.right,
        layer.sourceCrop.top, layer.sourceCrop.bottom);
    return false;
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
    return false;
  }
  return true;
}

bool IsValidComposition(int num_layers, vsoc_hwc_layer* layers) {
  // The FRAMEBUFFER_TARGET layer needs to be sane only if there is at least one
  // layer marked HWC_FRAMEBUFFER or if there is no layer marked HWC_OVERLAY
  // (i.e some layers where composed with OpenGL, no layer marked overlay or
  // framebuffer means that surfaceflinger decided to go for OpenGL without
  // asking the hwcomposer first)
  bool check_fb_target = true;
  for (int idx = 0; idx < num_layers; ++idx) {
    if (layers[idx].compositionType == HWC_FRAMEBUFFER) {
      // There is at least one, so it needs to be checked.
      // It may have been set to false before, so ensure it's set to true.
      check_fb_target = true;
      break;
    }
    if (layers[idx].compositionType == HWC_OVERLAY) {
      // At least one overlay, we may not need to.
      check_fb_target = false;
    }
  }

  for (int idx = 0; idx < num_layers; ++idx) {
    switch (layers[idx].compositionType) {
    case HWC_FRAMEBUFFER_TARGET:
      if (check_fb_target && !IsValidLayer(layers[idx])) {
        return false;
      }
      break;
    case HWC_OVERLAY:
      if (!(layers[idx].flags & HWC_SKIP_LAYER) &&
          !IsValidLayer(layers[idx])) {
        return false;
      }
      break;
    }
  }
  return true;
}

}

#if VSOC_PLATFORM_SDK_BEFORE(J_MR1)
static int vsoc_hwc_prepare(vsoc_hwc_device* dev, hwc_layer_list_t* list) {
#else
static int vsoc_hwc_prepare(vsoc_hwc_device* dev, size_t numDisplays,
                            hwc_display_contents_1_t** displays) {
  if (!numDisplays || !displays) return 0;

  hwc_display_contents_1_t* list = displays[HWC_DISPLAY_PRIMARY];

  if (!list) return 0;
#endif
  if (!IsValidComposition(list->numHwLayers, &list->hwLayers[0])) {
    LOG_FATAL("%s: Invalid composition requested", __FUNCTION__);
    return -1;
  }
  reinterpret_cast<vsoc_hwc_composer_device_1_t*>(dev)->composer->PrepareLayers(
      list->numHwLayers, &list->hwLayers[0]);
  return 0;
}

#if VSOC_PLATFORM_SDK_BEFORE(J_MR1)
int vsoc_hwc_set(struct hwc_composer_device* dev, hwc_display_t dpy,
                 hwc_surface_t sur, hwc_layer_list_t* list) {
  if (!IsValidComposition(list->numHwLayers, &list->hwLayers[0])) {
    LOG_FATAL("%s: Invalid composition requested", __FUNCTION__);
    return -1;
  }
  return reinterpret_cast<vsoc_hwc_composer_device_1_t*>(dev)
      ->composer->SetLayers(list->numHwLayers, &list->hwLayers[0]);
}
#else
static int vsoc_hwc_set(vsoc_hwc_device* dev, size_t numDisplays,
                        hwc_display_contents_1_t** displays) {
  if (!numDisplays || !displays) return 0;

  hwc_display_contents_1_t* contents = displays[HWC_DISPLAY_PRIMARY];
  if (!contents) return 0;

  vsoc_hwc_layer* layers = &contents->hwLayers[0];
  if (!IsValidComposition(contents->numHwLayers, layers)) {
    LOG_FATAL("%s: Invalid composition requested", __FUNCTION__);
    return -1;
  }
  int retval =
      reinterpret_cast<vsoc_hwc_composer_device_1_t*>(dev)->composer->SetLayers(
          contents->numHwLayers, layers);

  int closedFds = 0;
  for (size_t index = 0; index < contents->numHwLayers; ++index) {
    if (layers[index].acquireFenceFd != -1) {
      close(layers[index].acquireFenceFd);
      layers[index].acquireFenceFd = -1;
      ++closedFds;
    }
  }
  if (closedFds) {
    ALOGI("Saw %zu layers, closed=%d", contents->numHwLayers, closedFds);
  }

  // TODO(ghartman): This should be set before returning. On the next set it
  // should be signalled when we load the new frame.
  contents->retireFenceFd = -1;
  return retval;
}
#endif

static void vsoc_hwc_register_procs(vsoc_hwc_device* dev,
                                    const hwc_procs_t* procs) {
  struct vsoc_hwc_composer_device_1_t* pdev =
      (struct vsoc_hwc_composer_device_1_t*)dev;
  pdev->procs = procs;
}

static int vsoc_hwc_query(vsoc_hwc_device* dev, int what, int* value) {
  struct vsoc_hwc_composer_device_1_t* pdev =
      (struct vsoc_hwc_composer_device_1_t*)dev;

  switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
      // we support the background layer
      value[0] = 0;
      break;
    case HWC_VSYNC_PERIOD:
      value[0] = pdev->vsync_period_ns;
      break;
    default:
      // unsupported query
      ALOGE("%s badness unsupported query what=%d", __FUNCTION__, what);
      return -EINVAL;
  }
  return 0;
}

static int vsoc_hwc_event_control(
#if VSOC_PLATFORM_SDK_BEFORE(J_MR1)
    vsoc_hwc_device* /*dev*/, int event, int /*enabled*/) {
#else
    vsoc_hwc_device* /*dev*/, int /*dpy*/, int event, int /*enabled*/) {
#endif

  if (event == HWC_EVENT_VSYNC) {
    return 0;
  }
  return -EINVAL;
}

static void* hwc_vsync_thread(void* data) {
  struct vsoc_hwc_composer_device_1_t* pdev =
      (struct vsoc_hwc_composer_device_1_t*)data;
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

  int64_t base_timestamp = pdev->vsync_base_timestamp;
  int64_t last_logged = base_timestamp / 1e9;
  int sent = 0;
  int last_sent = 0;
  static const int log_interval = 60;
  void (*vsync_proc)(const struct hwc_procs*, int, int64_t) = nullptr;
  bool log_no_procs = true, log_no_vsync = true;
  while (true) {
    struct timespec rt;
    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
      ALOGE("%s:%d error in vsync thread clock_gettime: %s", __FILE__, __LINE__,
            strerror(errno));
    }
    int64_t timestamp = int64_t(rt.tv_sec) * 1e9 + rt.tv_nsec;
    // Given now's timestamp calculate the time of the next timestamp.
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

    // The vsync thread is started on device open, it may run before the
    // registerProcs callback has a chance to be called, so we need to make sure
    // procs is not NULL before dereferencing it.
    if (pdev && pdev->procs) {
      vsync_proc = pdev->procs->vsync;
    } else if (log_no_procs) {
      log_no_procs = false;
      ALOGI("procs is not set yet, unable to deliver vsync event");
    }
    if (vsync_proc) {
      vsync_proc(const_cast<hwc_procs_t*>(pdev->procs), 0, timestamp);
      ++sent;
    } else if (log_no_vsync) {
      log_no_vsync = false;
      ALOGE("vsync callback is null (but procs was already set)");
    }
    if (rt.tv_sec - last_logged > log_interval) {
      ALOGI("Sent %d syncs in %ds", sent - last_sent, log_interval);
      last_logged = rt.tv_sec;
      last_sent = sent;
    }
  }

  return NULL;
}

static int vsoc_hwc_blank(vsoc_hwc_device* /*dev*/, int disp, int /*blank*/) {
  if (!IS_PRIMARY_DISPLAY(disp)) return -EINVAL;
  return 0;
}

static void vsoc_hwc_dump(vsoc_hwc_device* dev, char* buff, int buff_len) {
  reinterpret_cast<vsoc_hwc_composer_device_1_t*>(dev)->composer->Dump(
      buff, buff_len);
}

static int vsoc_hwc_get_display_configs(vsoc_hwc_device* /*dev*/, int disp,
                                        uint32_t* configs, size_t* numConfigs) {
  if (*numConfigs == 0) return 0;

  if (IS_PRIMARY_DISPLAY(disp)) {
    configs[0] = 0;
    *numConfigs = 1;
    return 0;
  }

  return -EINVAL;
}

#if VSOC_PLATFORM_SDK_AFTER(J)
static int32_t vsoc_hwc_attribute(struct vsoc_hwc_composer_device_1_t* pdev,
                                  const uint32_t attribute) {
  auto screen_view = ScreenRegionView::GetInstance();
  switch (attribute) {
    case HWC_DISPLAY_VSYNC_PERIOD:
      return pdev->vsync_period_ns;
    case HWC_DISPLAY_WIDTH:
      return screen_view->x_res();
    case HWC_DISPLAY_HEIGHT:
      return screen_view->y_res();
    case HWC_DISPLAY_DPI_X:
      ALOGI("Reporting DPI_X of %d", screen_view->dpi());
      // The number of pixels per thousand inches
      return screen_view->dpi() * 1000;
    case HWC_DISPLAY_DPI_Y:
      ALOGI("Reporting DPI_Y of %d", screen_view->dpi());
      // The number of pixels per thousand inches
      return screen_view->dpi() * 1000;
    default:
      ALOGE("unknown display attribute %u", attribute);
      return -EINVAL;
  }
}

static int vsoc_hwc_get_display_attributes(vsoc_hwc_device* dev, int disp,
                                           uint32_t config __unused,
                                           const uint32_t* attributes,
                                           int32_t* values) {
  struct vsoc_hwc_composer_device_1_t* pdev =
      (struct vsoc_hwc_composer_device_1_t*)dev;

  if (!IS_PRIMARY_DISPLAY(disp)) {
    ALOGE("unknown display type %u", disp);
    return -EINVAL;
  }

  for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
    values[i] = vsoc_hwc_attribute(pdev, attributes[i]);
  }

  return 0;
}
#endif

static int vsoc_hwc_close(hw_device_t* device) {
  struct vsoc_hwc_composer_device_1_t* dev =
      (struct vsoc_hwc_composer_device_1_t*)device;
  ALOGE("vsoc_hwc_close");
  pthread_kill(dev->vsync_thread, SIGTERM);
  pthread_join(dev->vsync_thread, NULL);
  delete dev->composer;
  delete dev;
  return 0;
}

static int vsoc_hwc_open(const struct hw_module_t* module, const char* name,
                         struct hw_device_t** device) {
  ALOGI("%s", __FUNCTION__);
  if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
    ALOGE("%s called with bad name %s", __FUNCTION__, name);
    return -EINVAL;
  }

  vsoc_hwc_composer_device_1_t* dev = new vsoc_hwc_composer_device_1_t();
  if (!dev) {
    ALOGE("%s failed to allocate dev", __FUNCTION__);
    return -ENOMEM;
  }

  int refreshRate = ScreenRegionView::GetInstance()->refresh_rate_hz();
  dev->vsync_period_ns = 1000000000 / refreshRate;
  struct timespec rt;
  if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
    ALOGE("%s:%d error in vsync thread clock_gettime: %s", __FILE__, __LINE__,
          strerror(errno));
  }
  dev->vsync_base_timestamp = int64_t(rt.tv_sec) * 1e9 + rt.tv_nsec;

  dev->base.common.tag = HARDWARE_DEVICE_TAG;
  dev->base.common.version = VSOC_HWC_DEVICE_API_VERSION;
  dev->base.common.module = const_cast<hw_module_t*>(module);
  dev->base.common.close = vsoc_hwc_close;

  dev->base.prepare = vsoc_hwc_prepare;
  dev->base.set = vsoc_hwc_set;
  dev->base.query = vsoc_hwc_query;
  dev->base.registerProcs = vsoc_hwc_register_procs;
  dev->base.dump = vsoc_hwc_dump;
#if VSOC_PLATFORM_SDK_BEFORE(J_MR1)
  static hwc_methods_t hwc_methods = {vsoc_hwc_event_control};
  dev->base.methods = &hwc_methods;
#else
  dev->base.blank = vsoc_hwc_blank;
  dev->base.eventControl = vsoc_hwc_event_control;
  dev->base.getDisplayConfigs = vsoc_hwc_get_display_configs;
  dev->base.getDisplayAttributes = vsoc_hwc_get_display_attributes;
#endif
  dev->composer =
      new ComposerType(dev->vsync_base_timestamp, dev->vsync_period_ns);

  int ret = pthread_create(&dev->vsync_thread, NULL, hwc_vsync_thread, dev);
  if (ret) {
    ALOGE("failed to start vsync thread: %s", strerror(ret));
    ret = -ret;
    delete dev;
  } else {
    *device = &dev->base.common;
  }

  return ret;
}

static struct hw_module_methods_t vsoc_hwc_module_methods = {
    vsoc_hwc_open,
};

hwc_module_t HAL_MODULE_INFO_SYM = {{HARDWARE_MODULE_TAG,
                                     HWC_MODULE_API_VERSION_0_1,
                                     HARDWARE_HAL_API_VERSION,
                                     HWC_HARDWARE_MODULE_ID,
                                     "VSOC hwcomposer module",
                                     "Google",
                                     &vsoc_hwc_module_methods,
                                     NULL,
                                     {0}}};
