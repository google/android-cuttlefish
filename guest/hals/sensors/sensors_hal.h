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
#pragma once

#include <log/log.h>
#include <hardware/hardware.h>
#include <hardware/sensors.h>

#include "guest/libs/platform_support/api_level_fixes.h"

#if VSOC_PLATFORM_SDK_BEFORE(K)
#define VSOC_SENSOR_DEVICE_VERSION HARDWARE_MAKE_API_VERSION(0, 1)
#elif VSOC_PLATFORM_SDK_BEFORE(L)
#define VSOC_SENSOR_DEVICE_VERSION SENSORS_DEVICE_API_VERSION_1_1
#elif VSOC_PLATFORM_SDK_BEFORE(M)
#define VSOC_SENSOR_DEVICE_VERSION SENSORS_DEVICE_API_VERSION_1_3
#else
#define VSOC_SENSOR_DEVICE_VERSION SENSORS_DEVICE_API_VERSION_1_4
#endif

#define VSOC_SENSORS_DEVICE_API_VERSION_ATLEAST(X) \
    (defined (SENSORS_DEVICE_API_VERSION_##X) && \
     (VSOC_SENSOR_DEVICE_VERSION >= SENSORS_DEVICE_API_VERSION_##X))

#define SENSORS_DEBUG 0

#if SENSORS_DEBUG
#  define D(...) ALOGD(__VA_ARGS__)
#else
#  define D(...) ((void)0)
#endif

