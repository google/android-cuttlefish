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

#define LOG_TAG "hwc.cutf_cvm"

#include <cutils/properties.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>
#include <log/log.h>

#include "guest/hals/hwcomposer/common/hwcomposer.h"

#include "guest/hals/hwcomposer/cutf_cvm/vsocket_screen_view.h"

static int hwc_open(const struct hw_module_t* module, const char* name,
                    struct hw_device_t** device) {
  std::unique_ptr<cuttlefish::ScreenView> screen_view(new cuttlefish::VsocketScreenView());
  if (!screen_view) {
    ALOGE("Failed to instantiate screen view");
    return -1;
  }

  return cuttlefish::cvd_hwc_open(std::move(screen_view), module, name, device);
}

static struct hw_module_methods_t hwc_module_methods = {
    hwc_open,
};

hwc_module_t HAL_MODULE_INFO_SYM = {{HARDWARE_MODULE_TAG,
                                     HWC_MODULE_API_VERSION_0_1,
                                     HARDWARE_HAL_API_VERSION,
                                     HWC_HARDWARE_MODULE_ID,
                                     "VSOCKET hwcomposer module",
                                     "Google",
                                     &hwc_module_methods,
                                     NULL,
                                     {0}}};
