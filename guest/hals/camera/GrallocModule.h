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

class GrallocModule
{
public:
  static GrallocModule &getInstance() {
    static GrallocModule instance;
    return instance;
  }

  int lock(buffer_handle_t handle,
      int usage, int l, int t, int w, int h, void **vaddr) {
    return mModule->lock(mModule, handle, usage, l, t, w, h, vaddr);
  }

#ifdef GRALLOC_MODULE_API_VERSION_0_2
  int lock_ycbcr(buffer_handle_t handle,
      int usage, int l, int t, int w, int h,
      struct android_ycbcr *ycbcr) {
    return mModule->lock_ycbcr(mModule, handle, usage, l, t, w, h, ycbcr);
  }
#endif

  int unlock(buffer_handle_t handle) {
    return mModule->unlock(mModule, handle);
  }

private:
  GrallocModule() {
    const hw_module_t *module = NULL;
    int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
    if (ret) {
      ALOGE("%s: Failed to get gralloc module: %d", __FUNCTION__, ret);
    }
    mModule = reinterpret_cast<const gralloc_module_t*>(module);
  }
  const gralloc_module_t *mModule;
};

#endif  // GUEST_HALS_CAMERA_GRALLOCMODULE_H_
