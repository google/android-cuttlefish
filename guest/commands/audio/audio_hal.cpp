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
#include "guest/commands/audio/vsoc_audio.h"

#include "guest/commands/audio/audio_hal.h"
#include "guest/libs/platform_support/api_level_fixes.h"

static hw_module_methods_t hal_module_methods = {
  VSOC_STATIC_INITIALIZER(open) cvd::GceAudio::Open,
};


audio_module HAL_MODULE_INFO_SYM = {
  VSOC_STATIC_INITIALIZER(common) {
    VSOC_STATIC_INITIALIZER(tag) HARDWARE_MODULE_TAG,
    VSOC_STATIC_INITIALIZER(module_api_version) AUDIO_MODULE_API_VERSION_0_1,
    VSOC_STATIC_INITIALIZER(hal_api_version) HARDWARE_HAL_API_VERSION,
    VSOC_STATIC_INITIALIZER(id) AUDIO_HARDWARE_MODULE_ID,
    VSOC_STATIC_INITIALIZER(name) "GCE Audio HW HAL",
    VSOC_STATIC_INITIALIZER(author) "The Android Open Source Project",
    VSOC_STATIC_INITIALIZER(methods) &hal_module_methods,
  },
};
