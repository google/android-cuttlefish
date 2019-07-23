#pragma once
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

#include <memory>

#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>

#include "guest/libs/platform_support/api_level_fixes.h"

#include "guest/hals/hwcomposer/common/screen_view.h"

#if VSOC_PLATFORM_SDK_BEFORE(J_MR1)
typedef hwc_composer_device_t cvd_hwc_device;
typedef hwc_layer_t cvd_hwc_layer;
#define IS_TARGET_FRAMEBUFFER(x) false
#define IS_PRIMARY_DISPLAY(x) true
#define VSOC_HWC_DEVICE_API_VERSION HWC_DEVICE_API_VERSION_0_3
#else
typedef hwc_composer_device_1_t cvd_hwc_device;
typedef hwc_layer_1_t cvd_hwc_layer;
#define IS_TARGET_FRAMEBUFFER(x) ((x) == HWC_FRAMEBUFFER_TARGET)
#define IS_PRIMARY_DISPLAY(x) ((x) == HWC_DISPLAY_PRIMARY)
#define VSOC_HWC_DEVICE_API_VERSION HWC_DEVICE_API_VERSION_1_1
#endif

namespace cvd {
struct hwc_composer_device_data_t {
  const hwc_procs_t* procs;
  pthread_t vsync_thread;
  int64_t vsync_base_timestamp;
  int32_t vsync_period_ns;
};

void* hwc_vsync_thread(void* data);

int cvd_hwc_open(std::unique_ptr<ScreenView> screen_view,
                 const struct hw_module_t* module, const char* name,
                 struct hw_device_t** device);
}  // namespace cvd
