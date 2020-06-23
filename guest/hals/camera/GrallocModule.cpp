/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "GrallocModule.h"

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_GrallocModule"
#include <log/log.h>

using android::hardware::hidl_handle;

using V3Error = android::hardware::graphics::mapper::V3_0::Error;
using V3Mapper = android::hardware::graphics::mapper::V3_0::IMapper;
using V3YCbCrLayout = android::hardware::graphics::mapper::V3_0::YCbCrLayout;

GrallocModule::GrallocModule() {
  mGralloc3 = V3Mapper::getService();
  if (mGralloc3 != nullptr) {
    ALOGV("%s: Using gralloc 3.", __FUNCTION__);
    return;
  }

  const hw_module_t* module = NULL;
  int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
  if (ret) {
    ALOGE("%s: Failed to get gralloc module: %d", __FUNCTION__, ret);
  } else {
    ALOGV("%s: Using gralloc 0.", __FUNCTION__);
    mGralloc0 = reinterpret_cast<const gralloc_module_t *>(module);
    return;
  }

  ALOGE("%s: No gralloc available.", __FUNCTION__);
}

int GrallocModule::import(buffer_handle_t handle,
                          buffer_handle_t* imported_handle) {
  if (mGralloc3 != nullptr) {
    V3Error error = V3Error::NONE;
    auto ret = mGralloc3->importBuffer(
      handle,
      [&](const auto& tmp_err, const auto& tmp_buf) {
        error = tmp_err;
        if (error == V3Error::NONE) {
          *imported_handle = static_cast<buffer_handle_t>(tmp_buf);
        }
      });
    if (!ret.isOk() || error != V3Error::NONE) {
      ALOGE("%s: Failed to import gralloc3 buffer.", __FUNCTION__);
      return -1;
    }
    return 0;
  }

  if (mGralloc0 != nullptr) {
    int ret = mGralloc0->registerBuffer(mGralloc0, handle);
    if (ret) {
      ALOGE("%s: Failed to import gralloc0 buffer: %d.", __FUNCTION__, ret);
    }

    *imported_handle = handle;
    return ret;
  }

  ALOGE("%s: No gralloc available for import.", __FUNCTION__);
  return -1;
}

int GrallocModule::release(buffer_handle_t handle) {
  if (mGralloc3 != nullptr) {
    native_handle_t* native_handle = const_cast<native_handle_t*>(handle);

    auto ret = mGralloc3->freeBuffer(native_handle);
    if (!ret.isOk()) {
      ALOGE("%s: Failed to release gralloc3 buffer.", __FUNCTION__);
      return -1;
    }
    return 0;
  }

  if (mGralloc0 != nullptr) {
    int ret = mGralloc0->unregisterBuffer(mGralloc0, handle);
    if (ret) {
      ALOGE("%s: Failed to release gralloc0 buffer: %d.", __FUNCTION__, ret);
    }
    return ret;
  }

  ALOGE("%s: No gralloc available for release.", __FUNCTION__);
  return -1;
}

int GrallocModule::lock(buffer_handle_t handle, int usage, int l, int t, int w,
                        int h, void **vaddr) {
  if (mGralloc3 != nullptr) {
    native_handle_t* native_handle = const_cast<native_handle_t*>(handle);

    V3Mapper::Rect rect;
    rect.left = l;
    rect.top = t;
    rect.width = w;
    rect.height = h;

    hidl_handle empty_fence_handle;

    V3Error error = V3Error::NONE;
    auto ret = mGralloc3->lock(native_handle, usage, rect, empty_fence_handle,
                               [&](const auto& tmp_err,
                                   const auto& tmp_vaddr,
                                   const auto& /*bytes_per_pixel*/,
                                   const auto& /*bytes_per_stride*/) {
                                 error = tmp_err;
                                 if (tmp_err == V3Error::NONE) {
                                   *vaddr = tmp_vaddr;
                                 }
                               });
    if (!ret.isOk() || error != V3Error::NONE) {
      ALOGE("%s Failed to lock gralloc3 buffer.", __FUNCTION__);
      return -1;
    }
    return 0;
  }

  if (mGralloc0 != nullptr) {
    int ret = mGralloc0->lock(mGralloc0, handle, usage, l, t, w, h, vaddr);
    if (ret) {
      ALOGE("%s: Failed to lock gralloc0 buffer: %d", __FUNCTION__, ret);
    }
    return ret;
  }

  ALOGE("%s: No gralloc available for lock.", __FUNCTION__);
  return -1;
}

int GrallocModule::lock_ycbcr(buffer_handle_t handle, int usage, int l, int t,
                              int w, int h, struct android_ycbcr *ycbcr) {
  if (mGralloc3 != nullptr) {
    native_handle_t* native_handle = const_cast<native_handle_t*>(handle);

    V3Mapper::Rect rect;
    rect.left = l;
    rect.top = t;
    rect.width = w;
    rect.height = h;

    hidl_handle empty_fence_handle;

    V3YCbCrLayout ycbcr_layout = {};

    V3Error error;
    auto ret = mGralloc3->lockYCbCr(native_handle, usage, rect,
                                    empty_fence_handle,
                                    [&](const auto& tmp_err,
                                        const auto& tmp_ycbcr_layout) {
                                      error = tmp_err;
                                      if (tmp_err == V3Error::NONE) {
                                        ycbcr_layout = tmp_ycbcr_layout;
                                      }
                                    });

    if (!ret.isOk() || error != V3Error::NONE) {
      ALOGE("%s: Failed to lock_ycbcr gralloc3 buffer.", __FUNCTION__);
      return -1;
    }

    ycbcr->y = ycbcr_layout.y;
    ycbcr->cb = ycbcr_layout.cb;
    ycbcr->cr = ycbcr_layout.cr;
    ycbcr->ystride = ycbcr_layout.yStride;
    ycbcr->cstride = ycbcr_layout.cStride;
    ycbcr->chroma_step = ycbcr_layout.chromaStep;
    return 0;
  }

  if (mGralloc0 != nullptr) {
    int ret = mGralloc0->lock_ycbcr(mGralloc0, handle, usage, l, t, w, h,
                                    ycbcr);
    if (ret) {
      ALOGE("%s: Failed to lock_ycbcr gralloc0 buffer: %d", __FUNCTION__, ret);
    }
    return ret;
  }

  ALOGE("%s: No gralloc available for lock_ycbcr.", __FUNCTION__);
  return -1;
}

int GrallocModule::unlock(buffer_handle_t handle) {
  if (mGralloc3 != nullptr) {
    native_handle_t* native_handle = const_cast<native_handle_t*>(handle);

    V3Error error;
    auto ret = mGralloc3->unlock(native_handle,
                                 [&](const auto& tmp_err, const auto&) {
                                   error = tmp_err;
                                 });
    if (!ret.isOk() || error != V3Error::NONE) {
      ALOGE("%s: Failed to unlock gralloc3 buffer.", __FUNCTION__);
      return -1;
    }
    return 0;
  }

  if (mGralloc0 != nullptr) {
    int ret = mGralloc0->unlock(mGralloc0, handle);
    if (ret) {
      ALOGE("%s: Failed to unlock gralloc0 buffer: %d", __FUNCTION__, ret);
    }
    return ret;
  }

  ALOGE("%s: No gralloc available for unlock.", __FUNCTION__);
  return -1;
}