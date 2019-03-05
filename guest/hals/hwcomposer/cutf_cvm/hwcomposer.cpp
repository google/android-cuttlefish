/*
 * Copyright (C) 2019 The Android Open Source Project
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

#define LOG_TAG "hwc.cf_x86"
#define HWC_REMOVE_DEPRECATED_VERSIONS 1
#define IS_TARGET_FRAMEBUFFER(x) ((x) == HWC_FRAMEBUFFER_TARGET)
#define IS_PRIMARY_DISPLAY(x) ((x) == HWC_DISPLAY_PRIMARY)
#define CUTF_CVM_HWC_DEVICE_API_VERSION HWC_DEVICE_API_VERSION_1_1

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string>

#include "log/log.h"
#include "guest/hals/hwcomposer/common/hwcomposer.h"
#include "guest/libs/platform_support/api_level_fixes.h"
#include "hardware/hwcomposer.h"
#include "hardware/hwcomposer_defs.h"

typedef hwc_composer_device_1_t cutf_cvm_hwc_device;

struct cutf_cvm_hwc_composer_device_1_t {
  cutf_cvm_hwc_device base;
  cvd::hwc_composer_device_data_t vsync_data;
};

static int cutf_cvm_hwc_prepare(cutf_cvm_hwc_device* dev __unused, size_t numDisplays,
                            hwc_display_contents_1_t** displays) {
  hwc_display_contents_1_t* list = displays[HWC_DISPLAY_PRIMARY];
  if (!numDisplays || !displays || !list)
    return 0;

  for (size_t i = 0; i < list->numHwLayers; i++) {
    if (IS_TARGET_FRAMEBUFFER(list->hwLayers[i].compositionType)) {
      continue;
    }
    list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
  }
  return 0;
}

static int cutf_cvm_hwc_set(cutf_cvm_hwc_device* dev __unused, size_t numDisplays __unused,
                        hwc_display_contents_1_t** displays __unused) {
  // TODO: b/125482910
  return 0;
}

static void cutf_cvm_hwc_register_procs(cutf_cvm_hwc_device* dev,
                                    const hwc_procs_t* procs) {
  struct cutf_cvm_hwc_composer_device_1_t* pdev =
      (struct cutf_cvm_hwc_composer_device_1_t*)dev;
  pdev->vsync_data.procs = procs;
}

static int cutf_cvm_hwc_query(cutf_cvm_hwc_device* dev __unused, int what __unused, int* value) {
  // TODO: b/125482910
  struct cutf_cvm_hwc_composer_device_1_t* pdev =
      (struct cutf_cvm_hwc_composer_device_1_t*)dev;
  switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
      *value = 0;
      break;
    case HWC_VSYNC_PERIOD:
      *value = pdev->vsync_data.vsync_period_ns;
      break;
    default:
      // unsupported query
      ALOGE("%s: badness unsupported query what=%d", __FUNCTION__, what);
      return -EINVAL;
  }
  return 0;
}

static int cutf_cvm_hwc_event_control(
    cutf_cvm_hwc_device* /*dev*/, int /*dpy*/, int event __unused, int /*enabled*/) {
  // TODO: b/125482910
  return 0;
}

static int cutf_cvm_hwc_blank(cutf_cvm_hwc_device* /*dev*/, int disp __unused, int /*blank*/) {
  // TODO: b/125482910
  return 0;
}

static void cutf_cvm_hwc_dump(cutf_cvm_hwc_device* dev __unused, char* buff __unused, int buff_len __unused) {
  // TODO: b/125482910
}

static int cutf_cvm_hwc_get_display_configs(cutf_cvm_hwc_device* /*dev*/, int disp __unused,
                                        uint32_t* configs __unused, size_t* numConfigs __unused) {
  // TODO: b/125482910
  return 0;
}

static int32_t cutf_cvm_hwc_attribute(struct cutf_cvm_hwc_composer_device_1_t* pdev,
                                  const uint32_t attribute) {
  // TODO: b/125482910
  switch (attribute) {
    case HWC_DISPLAY_VSYNC_PERIOD:
      return pdev->vsync_data.vsync_period_ns;
    case HWC_DISPLAY_WIDTH:
      return 720;
    case HWC_DISPLAY_HEIGHT:
      return 1280;
    case HWC_DISPLAY_DPI_X:
      ALOGI("%s: Reporting DPI_X of %d", __FUNCTION__, 1);
      // The number of pixels per thousand inches
      return 240000;
    case HWC_DISPLAY_DPI_Y:
      ALOGI("%s: Reporting DPI_Y of %d", __FUNCTION__, 1);
      // The number of pixels per thousand inches
      return 240000;
    default:
      ALOGE("%s: unknown display attribute %u", __FUNCTION__, attribute);
      return -EINVAL;
  }
}

static int cutf_cvm_hwc_get_display_attributes(cutf_cvm_hwc_device* dev __unused, int disp __unused,
                                           uint32_t config __unused,
                                           const uint32_t* attributes __unused,
                                           int32_t* values __unused) {
  struct cutf_cvm_hwc_composer_device_1_t* pdev =
      (struct cutf_cvm_hwc_composer_device_1_t*)dev;

  if (!IS_PRIMARY_DISPLAY(disp)) {
    ALOGE("%s: unknown display type %u", __FUNCTION__, disp);
    return -EINVAL;
  }

  for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
    values[i] = cutf_cvm_hwc_attribute(pdev, attributes[i]);
  }

  return 0;
}

static int cutf_cvm_hwc_close(hw_device_t* device) {
  struct cutf_cvm_hwc_composer_device_1_t* dev =
      (struct cutf_cvm_hwc_composer_device_1_t*)device;
  delete dev;
  return 0;
}

static int cutf_cvm_hwc_open(const struct hw_module_t* module, const char* name,
                         struct hw_device_t** device) {
  ALOGI("%s", __FUNCTION__);
  if (strcmp(name, HWC_HARDWARE_COMPOSER) != 0) {
    ALOGE("%s: called with bad name %s", __FUNCTION__, name);
    return -EINVAL;
  }

  cutf_cvm_hwc_composer_device_1_t* dev = new cutf_cvm_hwc_composer_device_1_t();
  if (!dev) {
    ALOGE("%s: failed to allocate dev", __FUNCTION__);
    return -ENOMEM;
  }

  const int refreshRate = 60;
  const int nsPerSec = 1000000000;
  dev->vsync_data.vsync_period_ns = nsPerSec / refreshRate;
  struct timespec rt;
  if (clock_gettime(CLOCK_MONOTONIC, &rt) != 0) {
    ALOGE("%s: error in clock_gettime: %s", __FUNCTION__, strerror(errno));
  }
  dev->vsync_data.vsync_base_timestamp = int64_t(rt.tv_sec) * nsPerSec + rt.tv_nsec;

  dev->base.common.tag = HARDWARE_DEVICE_TAG;
  dev->base.common.version = CUTF_CVM_HWC_DEVICE_API_VERSION;
  dev->base.common.module = const_cast<hw_module_t*>(module);
  dev->base.common.close = cutf_cvm_hwc_close;

  dev->base.prepare = cutf_cvm_hwc_prepare;
  dev->base.set = cutf_cvm_hwc_set;
  dev->base.query = cutf_cvm_hwc_query;
  dev->base.registerProcs = cutf_cvm_hwc_register_procs;
  dev->base.dump = cutf_cvm_hwc_dump;
  dev->base.blank = cutf_cvm_hwc_blank;
  dev->base.eventControl = cutf_cvm_hwc_event_control;
  dev->base.getDisplayConfigs = cutf_cvm_hwc_get_display_configs;
  dev->base.getDisplayAttributes = cutf_cvm_hwc_get_display_attributes;

  int ret = pthread_create(&dev->vsync_data.vsync_thread, NULL, cvd::hwc_vsync_thread, &dev->vsync_data);
  if (ret != 0) {
    ALOGE("%s: failed to start vsync thread: %s", __FUNCTION__, strerror(ret));
    dev->base.common.close(&dev->base.common);
    return -ret;
  }

  *device = &dev->base.common;
  return ret;
}

static struct hw_module_methods_t cutf_cvm_hwc_module_methods = {
    cutf_cvm_hwc_open,
};

hwc_module_t HAL_MODULE_INFO_SYM = {{HARDWARE_MODULE_TAG,
                                     HWC_MODULE_API_VERSION_0_1,
                                     HARDWARE_HAL_API_VERSION,
                                     HWC_HARDWARE_MODULE_ID,
                                     "cutf_cvm hwcomposer module",
                                     "Google",
                                     &cutf_cvm_hwc_module_methods,
                                     NULL,
                                     {0}}};
