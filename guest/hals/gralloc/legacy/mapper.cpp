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
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cutils/hashmap.h>
#include <log/log.h>
#include <cutils/atomic.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>

#include "gralloc_vsoc_priv.h"
#include "region_registry.h"

#define DEBUG_REFERENCES 1
#define DEBUG_MAX_LOCK_LEVEL 20

/*****************************************************************************/

int gralloc_register_buffer(gralloc_module_t const* /*module*/,
                            buffer_handle_t handle) {
  if (private_handle_t::validate(handle) < 0) {
    return -EINVAL;
  }

  private_handle_t* hnd = (private_handle_t*)handle;
  if (reference_region(__FUNCTION__, hnd)) {
    return 0;
  } else {
    return -EIO;
  }
}

int gralloc_unregister_buffer(gralloc_module_t const* /*module*/,
                              buffer_handle_t handle) {
  if (private_handle_t::validate(handle) < 0) {
    return -EINVAL;
  }
  private_handle_t* hnd = (private_handle_t*)handle;
  return unreference_region("gralloc_unregister_buffer", hnd);
}

int gralloc_lock(
    gralloc_module_t const* /*module*/, buffer_handle_t handle, int /*usage*/,
    int /*l*/, int /*t*/, int /*w*/, int /*h*/,
    void** vaddr) {
  if (private_handle_t::validate(handle) < 0) {
    return -EINVAL;
  }
  if (!vaddr) {
    return -EINVAL;
  }
  private_handle_t* hnd = (private_handle_t*)handle;
#if DEBUG_REFERENCES
  if (hnd->lock_level > DEBUG_MAX_LOCK_LEVEL) {
    LOG_FATAL("%s: unbalanced lock detected. lock level = %d",
              __FUNCTION__, hnd->lock_level);
  }
  ++hnd->lock_level;
#endif
  void* base = reference_region("gralloc_lock", hnd);
  *vaddr = reinterpret_cast<unsigned char*>(base)
      + hnd->frame_offset;
  return 0;
}

int gralloc_unlock(
    gralloc_module_t const* /*module*/, buffer_handle_t handle) {
  if (private_handle_t::validate(handle) < 0) {
    return -EINVAL;
  }
  private_handle_t* hnd = (private_handle_t*) handle;
#if DEBUG_REFERENCES
  if (hnd->lock_level <= 0) {
    LOG_FATAL("%s unbalanced unlock detected. lock level = %d",
              __FUNCTION__, hnd->lock_level);
  }
  --hnd->lock_level;
#endif
  unreference_region("gralloc_unlock", hnd);
  return 0;
}

int gralloc_lock_ycbcr(
    gralloc_module_t const* /*module*/, buffer_handle_t handle, int /*usage*/,
    int /*l*/, int /*t*/, int /*w*/, int /*h*/,
    struct android_ycbcr* ycbcr) {
  if (private_handle_t::validate(handle) < 0) {
    return -EINVAL;
  }
  private_handle_t* hnd = (private_handle_t*)handle;
#if DEBUG_REFERENCES
  if (hnd->lock_level > DEBUG_MAX_LOCK_LEVEL) {
    LOG_FATAL("%s: unbalanced lock detected. lock level = %d",
              __FUNCTION__, hnd->lock_level);
  }
  ++hnd->lock_level;
#endif
  void* base = reference_region("gralloc_lock_ycbcr", hnd);
  formatToYcbcr(hnd->format, hnd->x_res, hnd->y_res, base, ycbcr);
  return 0;
}
