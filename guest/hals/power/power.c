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
 *
 * Based on the HiKeyPowerHAL
 */

#include <pthread.h>
#include <semaphore.h>
#include <cutils/properties.h>

#define LOG_TAG "VSoCPowerHAL"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>
#include "guest/libs/platform_support/api_level_fixes.h"

struct vsoc_power_module {
    struct power_module base;
    pthread_mutex_t lock;
};


#if VSOC_PLATFORM_SDK_AFTER(N_MR1)

static void vsoc_power_set_feature(struct power_module __unused *module,
                                  feature_t __unused hint,
                                  int __unused state) {
    return;
}

#elif VSOC_PLATFORM_SDK_AFTER(L)

static void vsoc_power_set_feature(struct power_module __unused *module,
                                  power_hint_t __unused hint,
                                  int __unused state) {
    return;
}

#endif

static void vsoc_power_hint(struct power_module __unused *module,
                           power_hint_t __unused hint,
                           void __unused *data) {
    return;
}

static void vsoc_power_set_interactive(struct power_module __unused *module,
                                      int __unused on) {
    return;
}

static  void vsoc_power_init(struct power_module __unused *module) {
    return;
}


/*
 * The power module wasn't opened at all in versions prior to 'O'. The module
 * pointer was reinterpretd as a device pointer. 'O' retains this behavior when
 * open is set to NULL. This code is using that mode.
 * For reference,
 * 'O': hardware/interfaces/power/1.0/default/Power.cpp
 * prior: frameworks/base/services/core/jni/com_android_server_power_PowerManagerService.cpp
 */
static struct hw_module_methods_t power_module_methods = {
    VSOC_STATIC_INITIALIZER(open) NULL
};


struct vsoc_power_module HAL_MODULE_INFO_SYM = {
  VSOC_STATIC_INITIALIZER(base) {
    .common = {
        VSOC_STATIC_INITIALIZER(tag) HARDWARE_MODULE_TAG,
        VSOC_STATIC_INITIALIZER(module_api_version) POWER_MODULE_API_VERSION_0_2,
        VSOC_STATIC_INITIALIZER(hal_api_version) HARDWARE_HAL_API_VERSION,
        VSOC_STATIC_INITIALIZER(id) POWER_HARDWARE_MODULE_ID,
        VSOC_STATIC_INITIALIZER(name) "VSoC Power HAL",
        VSOC_STATIC_INITIALIZER(author) "The Android Open Source Project",
        VSOC_STATIC_INITIALIZER(methods) &power_module_methods,
    },
    VSOC_STATIC_INITIALIZER(init) vsoc_power_init,
    VSOC_STATIC_INITIALIZER(setInteractive) vsoc_power_set_interactive,
    VSOC_STATIC_INITIALIZER(powerHint) vsoc_power_hint,
    // Before L_MR1 we don't have setFeature
#if VSOC_PLATFORM_SDK_AFTER(L)
    VSOC_STATIC_INITIALIZER(setFeature) vsoc_power_set_feature,
#endif
  },

  VSOC_STATIC_INITIALIZER(lock) PTHREAD_MUTEX_INITIALIZER,
};

