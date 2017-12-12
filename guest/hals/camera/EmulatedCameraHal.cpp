/*
 * Copyright (C) 2011 The Android Open Source Project
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

/*
 * Contains implementation of the camera HAL layer in the system running
 * under the emulator.
 *
 * This file contains only required HAL header, which directs all the API calls
 * to the EmulatedCameraFactory class implementation, wich is responsible for
 * managing emulated cameras.
 */

#include "EmulatedCameraFactory.h"
#include "guest/libs/platform_support/api_level_fixes.h"

/*
 * Required HAL header.
 */
camera_module_t HAL_MODULE_INFO_SYM = {
  VSOC_STATIC_INITIALIZER(common) {
         VSOC_STATIC_INITIALIZER(tag)                HARDWARE_MODULE_TAG,
#if VSOC_PLATFORM_SDK_AFTER(K)
         VSOC_STATIC_INITIALIZER(module_api_version) CAMERA_MODULE_API_VERSION_2_3,
#elif VSOC_PLATFORM_SDK_AFTER(J_MR2)
         VSOC_STATIC_INITIALIZER(module_api_version) CAMERA_MODULE_API_VERSION_2_2,
#else
         VSOC_STATIC_INITIALIZER(module_api_version) CAMERA_MODULE_API_VERSION_2_0,
#endif
         VSOC_STATIC_INITIALIZER(hal_api_version)    HARDWARE_HAL_API_VERSION,
         VSOC_STATIC_INITIALIZER(id)                 CAMERA_HARDWARE_MODULE_ID,
         VSOC_STATIC_INITIALIZER(name)               "Emulated Camera Module",
         VSOC_STATIC_INITIALIZER(author)             "The Android Open Source Project",
         VSOC_STATIC_INITIALIZER(methods)            &android::EmulatedCameraFactory::mCameraModuleMethods,
         VSOC_STATIC_INITIALIZER(dso)                NULL,
         VSOC_STATIC_INITIALIZER(reserved)           {0},
    },
    VSOC_STATIC_INITIALIZER(get_number_of_cameras)  android::EmulatedCameraFactory::get_number_of_cameras,
    VSOC_STATIC_INITIALIZER(get_camera_info)        android::EmulatedCameraFactory::get_camera_info,
#if VSOC_PLATFORM_SDK_AFTER(J_MR2)
    VSOC_STATIC_INITIALIZER(set_callbacks)          android::EmulatedCameraFactory::set_callbacks,
    VSOC_STATIC_INITIALIZER(get_vendor_tag_ops)     android::EmulatedCameraFactory::get_vendor_tag_ops,
#endif
#if VSOC_PLATFORM_SDK_AFTER(K)
    VSOC_STATIC_INITIALIZER(open_legacy)            android::EmulatedCameraFactory::open_legacy
#endif
};
