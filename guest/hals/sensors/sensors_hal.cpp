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

static hw_module_methods_t hal_module_methods = {
  .open = cvd::GceSensors::Open,
};

sensors_module_t HAL_MODULE_INFO_SYM = {
  .common = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = 1,
    .hal_api_version = 0,
    .id = SENSORS_HARDWARE_MODULE_ID,
    .name = "Android-GCE SENSORS Module",
    .author = "Google",
    .methods = & hal_module_methods,
  },
  .get_sensors_list = cvd::GceSensors::GetSensorsList,
  .set_operation_mode = cvd::GceSensors::SetOperationMode,
};
