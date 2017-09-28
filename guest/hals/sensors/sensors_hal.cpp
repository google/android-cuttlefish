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
#include "guest/hals/sensors/sensors_hal.h"

#include "guest/hals/sensors/vsoc_sensors.h"
#include "guest/libs/platform_support/api_level_fixes.h"

static hw_module_methods_t hal_module_methods = {
  VSOC_STATIC_INITIALIZER(open) avd::GceSensors::Open,
};

sensors_module_t HAL_MODULE_INFO_SYM = {
  VSOC_STATIC_INITIALIZER(common){
    VSOC_STATIC_INITIALIZER(tag) HARDWARE_MODULE_TAG,
    VSOC_STATIC_INITIALIZER(module_api_version) 1,
    VSOC_STATIC_INITIALIZER(hal_api_version) 0,
    VSOC_STATIC_INITIALIZER(id) SENSORS_HARDWARE_MODULE_ID,
    VSOC_STATIC_INITIALIZER(name) "Android-GCE SENSORS Module",
    VSOC_STATIC_INITIALIZER(author) "Google",
    VSOC_STATIC_INITIALIZER(methods) & hal_module_methods,
  },
  VSOC_STATIC_INITIALIZER(get_sensors_list) avd::GceSensors::GetSensorsList,
#if VSOC_SENSORS_DEVICE_API_VERSION_ATLEAST(1_4)
  VSOC_STATIC_INITIALIZER(set_operation_mode) avd::GceSensors::SetOperationMode,
#endif
};
