#pragma once
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

#include <cutils/native_handle.h>
#include <hardware/gralloc.h>
#include <log/log.h>

struct vsoc_alloc_device_t {
  alloc_device_t device;
};

struct vsoc_gralloc_module_t {
  gralloc_module_t base;
};

static_assert(sizeof(int) >= 4, "At least 4 bytes are needed for offsets");

struct vsoc_buffer_handle_t : public native_handle {
  // File descriptors
  int fd;
  // ints
  int magic;
  int format;
  int x_res;
  int y_res;
  int stride_in_pixels;
  int size;
  // buffer offset in bytes divided by PAGE_SIZE
  int offset;

  static inline int sNumInts() {
    return ((sizeof(vsoc_buffer_handle_t) - sizeof(native_handle_t)) /
                sizeof(int) -
            sNumFds);
  }
  static const int sNumFds = 1;
  static const int sMagic = 0xc63752f4;

  vsoc_buffer_handle_t(int fd,
                       int offset,
                       int size,
                       int format,
                       int x_res,
                       int y_res,
                       int stride_in_pixels)
      : fd(fd),
        magic(sMagic),
        format(format),
        x_res(x_res),
        y_res(y_res),
        stride_in_pixels(stride_in_pixels),
        size(size),
        offset(offset) {
    version = sizeof(native_handle);
    numInts = sNumInts();
    numFds = sNumFds;
  }

  ~vsoc_buffer_handle_t() {
    magic = 0;
  }

  static int validate(const native_handle* handle) {
    const vsoc_buffer_handle_t* hnd =
        reinterpret_cast<const vsoc_buffer_handle_t*>(handle);
    if (!hnd || hnd->version != sizeof(native_handle) ||
        hnd->numInts != sNumInts() || hnd->numFds != sNumFds ||
        hnd->magic != sMagic) {
      ALOGE("Invalid gralloc handle (at %p)", handle);
      return -EINVAL;
    }
    return 0;
  }
};

// These functions are to be used to map/unmap gralloc buffers. They are thread
// safe and ensure that the same buffer never gets mapped twice.
void* reference_buffer(const vsoc_buffer_handle_t* hnd);
int unreference_buffer(const vsoc_buffer_handle_t* hnd);

// TODO(jemoreira): Move this to a place where it can be used by the gralloc
// region as well.
inline int align(int input, int alignment) {
  return (input + alignment - 1) & -alignment;
}
