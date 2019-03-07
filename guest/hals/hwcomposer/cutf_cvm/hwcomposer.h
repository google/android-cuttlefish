#pragma once
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

#include <guest/libs/platform_support/api_level_fixes.h>

#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>

#if VSOC_PLATFORM_SDK_BEFORE(J_MR1)
typedef hwc_composer_device_t vsoc_hwc_device;
typedef hwc_layer_t vsoc_hwc_layer;
#define IS_TARGET_FRAMEBUFFER(x) false
#define IS_PRIMARY_DISPLAY(x) true
#define VSOC_HWC_DEVICE_API_VERSION HWC_DEVICE_API_VERSION_0_3
#else
typedef hwc_composer_device_1_t vsoc_hwc_device;
typedef hwc_layer_1_t vsoc_hwc_layer;
#define IS_TARGET_FRAMEBUFFER(x) ((x) == HWC_FRAMEBUFFER_TARGET)
#define IS_PRIMARY_DISPLAY(x) ((x) == HWC_DISPLAY_PRIMARY)
#define VSOC_HWC_DEVICE_API_VERSION HWC_DEVICE_API_VERSION_1_1
#endif
