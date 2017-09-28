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
// Google Compute Engine (GCE) Audio HAL - Main File.
#include "audio_hal.h"
#include "gce_audio.h"


static hw_module_methods_t hal_module_methods = {
  open: avd::GceAudio::Open,
};


audio_module HAL_MODULE_INFO_SYM = {
  common: {
    tag: HARDWARE_MODULE_TAG,
    module_api_version: AUDIO_MODULE_API_VERSION_0_1,
    hal_api_version: HARDWARE_HAL_API_VERSION,
    id: AUDIO_HARDWARE_MODULE_ID,
    name: "GCE Audio HW HAL",
    author: "The Android Open Source Project",
    methods: &hal_module_methods,
  },
};
