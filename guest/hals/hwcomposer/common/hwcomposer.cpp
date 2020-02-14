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

#define LOG_TAG "hwc.cf_x86"
#define HWC_REMOVE_DEPRECATED_VERSIONS 1

#include "guest/hals/hwcomposer/common/hwcomposer.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sync/sync.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <sstream>
#include <string>
#include <vector>

#include <cutils/compiler.h>
#include <cutils/properties.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>
#include <log/log.h>
#include <utils/String8.h>
#include <utils/Vector.h>

#include "guest/hals/gralloc/legacy/gralloc_vsoc_priv.h"

#include "guest/hals/hwcomposer/common/base_composer.h"
#include "guest/hals/hwcomposer/common/cpu_composer.h"
#include "guest/hals/hwcomposer/common/geometry_utils.h"
#include "guest/hals/hwcomposer/common/hwcomposer.h"

#ifdef USE_OLD_HWCOMPOSER
typedef cvd::BaseComposer ComposerType;
#else
typedef cvd::CpuComposer ComposerType;
#endif

struct hwc_composer_device_data_t {
  const hwc_procs_t* procs;
  pthread_t vsync_thread;
  int64_t vsync_base_timestamp;
  int32_t vsync_period_ns;
};

struct cvd_hwc_composer_device_1_t {
  hwc_composer_device_1_t base;
  hwc_composer_device_data_t vsync_data;
  cvd::BaseComposer* composer;
};

struct external_display_config_t {
  uint64_t physicalId;
  uint32_t width;
  uint32_t height;
  uint32_t dpi;
  uint32_t flags;
};

namespace {

void* hwc_vsync_thread(void* data) {
  struct hwc_composer_device_data_t* pdev =
      (struct hwc_composer_device_data_t*)data;
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
      LOG_ALWAYS_FATAL("%s:%d error in vsync thread clock_gettime: %s",
                       __FILE__, __LINE__, strerror(errno));
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

std::string CompositionString(int type) {
  switch (type) {
    case HWC_FRAMEBUFFER:
      return "Framebuffer";
    case HWC_OVERLAY:
      return "Overlay";
    case HWC_BACKGROUND:
      return "Background";
    case HWC_FRAMEBUFFER_TARGET:
      return "FramebufferTarget";
    case HWC_SIDEBAND:
      return "Sideband";
    case HWC_CURSOR_OVERLAY:
      return "CursorOverlay";
    default:
      return std::string("Unknown (") + std::to_string(type) + ")";
  }
}

void LogLayers(int num_layers, hwc_layer_1_t* layers, int invalid) {
  ALOGE("Layers:");
  for (int idx = 0; idx < num_layers; ++idx) {
    std::string log_line;
    if (idx == invalid) {
      log_line = "Invalid layer: ";
    }
    log_line +=
        "Composition Type: " + CompositionString(layers[idx].compositionType);
    ALOGE("%s", log_line.c_str());
  }
}

// Ensures that the layer does not include any inconsistencies
bool IsValidLayer(const hwc_layer_1_t& layer) {
  if (layer.flags & HWC_SKIP_LAYER) {
    // A layer we are asked to skip validate should not be marked as skip
    ALOGE("%s: Layer is marked as skip", __FUNCTION__);
    return false;
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

bool IsValidComposition(int num_layers, hwc_layer_1_t* layers, bool on_set) {
  if (num_layers == 0) {
    ALOGE("Composition requested with 0 layers");
    return false;
  }
  // Sometimes the hwcomposer receives a prepare and set calls with no other
  // layer than the FRAMEBUFFER_TARGET with a null handler. We treat this case
  // independently as a valid composition, but issue a warning about it.
  if (num_layers == 1 && layers[0].compositionType == HWC_FRAMEBUFFER_TARGET &&
      layers[0].handle == NULL) {
    ALOGW("Received request for empty composition, treating as valid noop");
    return true;
  }
  // The FRAMEBUFFER_TARGET layer needs to be sane only if
  // there is at least one layer marked HWC_FRAMEBUFFER or if there is no layer
  // marked HWC_OVERLAY (i.e some layers where composed with OpenGL, no layer
  // marked overlay or framebuffer means that surfaceflinger decided to go for
  // OpenGL without asking the hwcomposer first)
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
        // In the call to prepare() the framebuffer target does not have a valid
        // buffer_handle, so we don't validate it yet.
        if (on_set && check_fb_target && !IsValidLayer(layers[idx])) {
          ALOGE("%s: Invalid layer found", __FUNCTION__);
          LogLayers(num_layers, layers, idx);
          return false;
        }
        break;
      case HWC_OVERLAY:
        if (!(layers[idx].flags & HWC_SKIP_LAYER) &&
            !IsValidLayer(layers[idx])) {
          ALOGE("%s: Invalid layer found", __FUNCTION__);
          LogLayers(num_layers, layers, idx);
          return false;
        }
        break;
    }
  }
  return true;
}

// Note predefined "hwservicemanager." is used to avoid adding new selinux rules
#define EXTERANL_DISPLAY_PROP "hwservicemanager.external.displays"

// return 0 for successful
// return < 0 if failed
int GetExternalDisplayConfigs(std::vector<struct external_display_config_t>* configs) {
  // this guest property, hwservicemanager.external.displays,
  // specifies multi-display info, with comma (,) as separator
  // each display has the following info:
  //   physicalId,width,height,dpi,flags
  // several displays can be provided, e.g., following has 2 displays:
  // setprop hwservicemanager.external.displays 1,1200,800,120,0,2,1200,800,120,0
  std::vector<uint64_t> values;
  char displays_value[PROPERTY_VALUE_MAX] = "";
  property_get(EXTERANL_DISPLAY_PROP, displays_value, "");
  bool valid = displays_value[0] != '\0';
  if (valid) {
      char *p = displays_value;
      while (*p) {
          if (!isdigit(*p) && *p != ',' && *p != ' ') {
              valid = false;
              break;
          }
          p++;
      }
  }
  if (!valid) {
      // no external displays are specified
      ALOGE("%s: Invalid syntax for the value of system prop: %s, value: %s",
          __FUNCTION__, EXTERANL_DISPLAY_PROP, displays_value);
      return 0;
  }
  // parse all int values to a vector
  std::istringstream stream(displays_value);
  for (uint64_t id; stream >> id;) {
      values.push_back(id);
      if (stream.peek() == ',')
          stream.ignore();
  }
  // each display has 5 values
  if ((values.size() % 5) != 0) {
      ALOGE("%s: Invalid value for system property: %s", __FUNCTION__, EXTERANL_DISPLAY_PROP);
      return -1;
  }
  while (!values.empty()) {
      struct external_display_config_t config;
      config.physicalId = values[0];
      config.width = values[1];
      config.height = values[2];
      config.dpi = values[3];
      config.flags = values[4];
      values.erase(values.begin(), values.begin() + 5);
      configs->push_back(config);
  }
  return 0;
}

}  // namespace

static int cvd_hwc_prepare(hwc_composer_device_1_t* dev, size_t numDisplays,
                           hwc_display_contents_1_t** displays) {
  if (!numDisplays || !displays) return 0;

  for (int disp = 0; disp < numDisplays; ++disp) {
    hwc_display_contents_1_t* list = displays[disp];

    if (!list) return 0;
    if (!IsValidComposition(list->numHwLayers, &list->hwLayers[0], false)) {
      LOG_ALWAYS_FATAL("%s: Invalid composition requested", __FUNCTION__);
      return -1;
    }
    reinterpret_cast<cvd_hwc_composer_device_1_t*>(dev)->composer->PrepareLayers(
      list->numHwLayers, &list->hwLayers[0]);
  }
  return 0;
}

static int cvd_hwc_set(hwc_composer_device_1_t* dev, size_t numDisplays,
                       hwc_display_contents_1_t** displays) {
  if (!numDisplays || !displays) return 0;

  int retval = -1;
  for (int disp = 0; disp < numDisplays; ++disp) {
    hwc_display_contents_1_t* contents = displays[disp];
    if (!contents) return 0;

    hwc_layer_1_t* layers = &contents->hwLayers[0];
    if (contents->numHwLayers == 1 &&
      layers[0].compositionType == HWC_FRAMEBUFFER_TARGET) {
      ALOGW("Received request for empty composition, treating as valid noop");
      return 0;
    }
    if (!IsValidComposition(contents->numHwLayers, layers, true)) {
      LOG_ALWAYS_FATAL("%s: Invalid composition requested", __FUNCTION__);
      return -1;
    }
    retval =
        reinterpret_cast<cvd_hwc_composer_device_1_t*>(dev)->composer->SetLayers(
            contents->numHwLayers, layers);
    if (retval != 0) break;

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
  }

  return retval;
}

static void cvd_hwc_register_procs(hwc_composer_device_1_t* dev,
                                   const hwc_procs_t* procs) {
  struct cvd_hwc_composer_device_1_t* pdev =
      (struct cvd_hwc_composer_device_1_t*)dev;
  pdev->vsync_data.procs = procs;
  if (procs) {
      std::vector<struct external_display_config_t> configs;
      int res = GetExternalDisplayConfigs(&configs);
      if (res == 0 && !configs.empty()) {
          // configs will be used in the future
          procs->hotplug(procs, HWC_DISPLAY_EXTERNAL, 1);
      }
  }
}

static int cvd_hwc_query(hwc_composer_device_1_t* dev, int what, int* value) {
  struct cvd_hwc_composer_device_1_t* pdev =
      (struct cvd_hwc_composer_device_1_t*)dev;

  switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
      // we support the background layer
      value[0] = 0;
      break;
    case HWC_VSYNC_PERIOD:
      value[0] = pdev->vsync_data.vsync_period_ns;
      break;
    default:
      // unsupported query
      ALOGE("%s badness unsupported query what=%d", __FUNCTION__, what);
      return -EINVAL;
  }
  return 0;
}

static int cvd_hwc_event_control(hwc_composer_device_1_t* /*dev*/, int /*dpy*/,
                                 int event, int /*enabled*/) {
  if (event == HWC_EVENT_VSYNC) {
    return 0;
  }
  return -EINVAL;
}

static int cvd_hwc_blank(hwc_composer_device_1_t* /*dev*/, int disp, int /*blank*/) {
  if (!IS_PRIMARY_DISPLAY(disp) && !IS_EXTERNAL_DISPLAY(disp)) return -EINVAL;
  return 0;
}

static void cvd_hwc_dump(hwc_composer_device_1_t* dev, char* buff, int buff_len) {
  reinterpret_cast<cvd_hwc_composer_device_1_t*>(dev)->composer->Dump(buff,
                                                                      buff_len);
}

static int cvd_hwc_get_display_configs(hwc_composer_device_1_t* /*dev*/, int disp,
                                       uint32_t* configs, size_t* numConfigs) {
  if (*numConfigs == 0) return 0;

  if (IS_PRIMARY_DISPLAY(disp) || IS_EXTERNAL_DISPLAY(disp)) {
    configs[0] = 0;
    *numConfigs = 1;
    return 0;
  }

  return -EINVAL;
}

static int32_t cvd_hwc_attribute(struct cvd_hwc_composer_device_1_t* pdev,
                                 const uint32_t attribute) {
  switch (attribute) {
    case HWC_DISPLAY_VSYNC_PERIOD:
      return pdev->vsync_data.vsync_period_ns;
    case HWC_DISPLAY_WIDTH:
      return pdev->composer->x_res();
    case HWC_DISPLAY_HEIGHT:
      return pdev->composer->y_res();
    case HWC_DISPLAY_DPI_X:
      ALOGI("Reporting DPI_X of %d", pdev->composer->dpi());
      // The number of pixels per thousand inches
      return pdev->composer->dpi() * 1000;
    case HWC_DISPLAY_DPI_Y:
      ALOGI("Reporting DPI_Y of %d", pdev->composer->dpi());
      // The number of pixels per thousand inches
      return pdev->composer->dpi() * 1000;
    default:
      ALOGE("unknown display attribute %u", attribute);
      return -EINVAL;
  }
}

static int cvd_hwc_get_display_attributes(hwc_composer_device_1_t* dev, int disp,
                                          uint32_t config __unused,
                                          const uint32_t* attributes,
                                          int32_t* values) {
  struct cvd_hwc_composer_device_1_t* pdev =
      (struct cvd_hwc_composer_device_1_t*)dev;
  if (!IS_PRIMARY_DISPLAY(disp) && !IS_EXTERNAL_DISPLAY(disp)) {
    ALOGE("unknown display type %u", disp);
    return -EINVAL;
  }

  for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
    values[i] = cvd_hwc_attribute(pdev, attributes[i]);
  }

  return 0;
}

static int cvd_hwc_close(hw_device_t* device) {
  struct cvd_hwc_composer_device_1_t* dev =
      (struct cvd_hwc_composer_device_1_t*)device;
  ALOGE("cvd_hwc_close");
  pthread_kill(dev->vsync_data.vsync_thread, SIGTERM);
  pthread_join(dev->vsync_data.vsync_thread, NULL);
  delete dev->composer;
  delete dev;
  return 0;
}

namespace cvd {

int cvd_hwc_open(std::unique_ptr<ScreenView> screen_view,
                 const struct hw_module_t* module, const char* name,
                 struct hw_device_t** device) {
  ALOGI("%s", __FUNCTION__);
  if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
    ALOGE("%s called with bad name %s", __FUNCTION__, name);
    return -EINVAL;
  }

  cvd_hwc_composer_device_1_t* dev = new cvd_hwc_composer_device_1_t();
  if (!dev) {
    ALOGE("%s failed to allocate dev", __FUNCTION__);
    return -ENOMEM;
  }

  struct timespec rt;
  if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
    ALOGE("%s:%d error in vsync thread clock_gettime: %s", __FILE__, __LINE__,
          strerror(errno));
  }
  dev->vsync_data.vsync_base_timestamp = int64_t(rt.tv_sec) * 1e9 + rt.tv_nsec;
  dev->vsync_data.vsync_period_ns = 1e9 / screen_view->refresh_rate();

  dev->base.common.tag = HARDWARE_DEVICE_TAG;
  dev->base.common.version = HWC_DEVICE_API_VERSION_1_1;
  dev->base.common.module = const_cast<hw_module_t*>(module);
  dev->base.common.close = cvd_hwc_close;

  dev->base.prepare = cvd_hwc_prepare;
  dev->base.set = cvd_hwc_set;
  dev->base.query = cvd_hwc_query;
  dev->base.registerProcs = cvd_hwc_register_procs;
  dev->base.dump = cvd_hwc_dump;
  dev->base.blank = cvd_hwc_blank;
  dev->base.eventControl = cvd_hwc_event_control;
  dev->base.getDisplayConfigs = cvd_hwc_get_display_configs;
  dev->base.getDisplayAttributes = cvd_hwc_get_display_attributes;
#ifdef GATHER_STATS
  dev->composer = new cvd::StatsKeepingComposer<ComposerType>(
      dev->vsync_data.vsync_base_timestamp, std::move(screen_view));
#else
  dev->composer = new ComposerType(std::move(screen_view));
#endif

  if (!dev->composer) {
    ALOGE("Failed to instantiate the composer object");
    delete dev;
    return -1;
  }
  int ret = pthread_create(&dev->vsync_data.vsync_thread, NULL,
                           hwc_vsync_thread, &dev->vsync_data);
  if (ret) {
    ALOGE("failed to start vsync thread: %s", strerror(ret));
    ret = -ret;
    delete dev->composer;
    delete dev;
  } else {
    *device = &dev->base.common;
  }

  return ret;
}

}  // namespace cvd
