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
#ifndef GUEST_HALS_CAMERA_GRALLOCMODULE_H_
#define GUEST_HALS_CAMERA_GRALLOCMODULE_H_

#include <hardware/gralloc.h>

#include <android/hardware/graphics/mapper/3.0/IMapper.h>
#include <utils/StrongPointer.h>

class GrallocModule {
 public:
  static GrallocModule &getInstance() {
    static GrallocModule instance;
    return instance;
  }

  int import(buffer_handle_t handle, buffer_handle_t* imported_handle);

  int release(buffer_handle_t handle);

  int lock(buffer_handle_t handle, int usage, int l, int t, int w, int h,
           void **vaddr);
  int lock_ycbcr(buffer_handle_t handle, int usage, int l, int t, int w, int h,
                 struct android_ycbcr *ycbcr);

  int unlock(buffer_handle_t handle);

 private:
  GrallocModule();

  const gralloc_module_t *mGralloc0;

  android::sp<android::hardware::graphics::mapper::V3_0::IMapper> mGralloc3;
};

#endif  // GUEST_HALS_CAMERA_GRALLOCMODULE_H_
