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
// Google Compute Engine (GCE) Sensors HAL - Main File
#include <api_level_fixes.h>

#include "sensors_hal.h"
#include "gce_sensors.h"

static hw_module_methods_t hal_module_methods = {
	GCE_STATIC_INITIALIZER(open) avd::GceSensors::Open,
};

sensors_module_t HAL_MODULE_INFO_SYM = {
    GCE_STATIC_INITIALIZER(common) {
        GCE_STATIC_INITIALIZER(tag) HARDWARE_MODULE_TAG,
        GCE_STATIC_INITIALIZER(module_api_version) 1,
        GCE_STATIC_INITIALIZER(hal_api_version) 0,
        GCE_STATIC_INITIALIZER(id) SENSORS_HARDWARE_MODULE_ID,
        GCE_STATIC_INITIALIZER(name) "Android-GCE SENSORS Module",
        GCE_STATIC_INITIALIZER(author) "Google",
        GCE_STATIC_INITIALIZER(methods) &hal_module_methods,
    },
    GCE_STATIC_INITIALIZER(get_sensors_list) avd::GceSensors::GetSensorsList,
#if GCE_SENSORS_DEVICE_API_VERSION_ATLEAST(1_4)
    GCE_STATIC_INITIALIZER(set_operation_mode) avd::GceSensors::SetOperationMode,
#endif
};
