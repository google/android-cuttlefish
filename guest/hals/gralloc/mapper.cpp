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

#include "gralloc_vsoc_priv.h"

#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#include <cutils/hashmap.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <log/log.h>

namespace {

const size_t g_page_size = sysconf(_SC_PAGESIZE);

struct HmLockGuard {
  HmLockGuard(Hashmap* map) : map_(map) {
    hashmapLock(map_);
  }
  ~HmLockGuard() {
    hashmapUnlock(map_);
  }
 private:
  Hashmap* map_;
};

int offset_hash(void* key) {
  return *reinterpret_cast<int*>(key);
}

bool offset_equals(void* key1, void* key2) {
  return *reinterpret_cast<int*>(key1) ==
         *reinterpret_cast<int*>(key2);
}

// Keeps track of how many times a buffer is locked in the current process.
struct GrallocBuffer {
  void* vaddr;
  int ref_count;
  GrallocBuffer() : vaddr(NULL), ref_count(0) {}

  static Hashmap* mapped_buffers() {
    static Hashmap* mapped_buffers =
        hashmapCreate(19, offset_hash, offset_equals);
    return mapped_buffers;
  }
};

}

void* reference_buffer(const vsoc_buffer_handle_t* hnd) {
  Hashmap* map = GrallocBuffer::mapped_buffers();
  HmLockGuard lock_guard(map);
  GrallocBuffer* buffer = reinterpret_cast<GrallocBuffer*>(
      hashmapGet(map, const_cast<int*>(&hnd->offset)));
  if (!buffer) {
    buffer = new GrallocBuffer();
    hashmapPut(map, const_cast<int*>(&hnd->offset), buffer);
  }

  if (!buffer->vaddr) {
    void* mapped =
      mmap(NULL, hnd->size, PROT_READ | PROT_WRITE, MAP_SHARED, hnd->fd, 0);
    if (mapped == MAP_FAILED) {
      ALOGE("Unable to map buffer (offset: %d, size: %d): %s",
            hnd->offset,
            hnd->size,
            strerror(errno));
      return NULL;
    }
    // Set up the guard pages. The last page is always a guard
    uintptr_t base = uintptr_t(mapped);
    uintptr_t addr = base + hnd->size - g_page_size;
    if (mprotect((void*)addr, g_page_size, PROT_NONE) == -1) {
      ALOGW("Unable to protect last page of buffer (offset: %d, size: %d): %s",
            hnd->offset,
            hnd->size,
            strerror(errno));
    }
    buffer->vaddr = mapped;
  }
  buffer->ref_count++;
  return buffer->vaddr;
}

int unreference_buffer(const vsoc_buffer_handle_t* hnd) {
  int result = 0;
  Hashmap* map = GrallocBuffer::mapped_buffers();
  HmLockGuard lock_guard(map);
  GrallocBuffer* buffer = reinterpret_cast<GrallocBuffer*>(
      hashmapGet(map, const_cast<int*>(&hnd->offset)));
  if (!buffer) {
    ALOGE("Unreferencing an unknown buffer (offset: %d, size: %d)",
          hnd->offset,
          hnd->size);
    return -EINVAL;
  }
  if (buffer->ref_count == 0) {
    ALOGE("Unbalanced reference/unreference on buffer (offset: %d, size: %d)",
          hnd->offset,
          hnd->size);
    return -EINVAL;
  }
  buffer->ref_count--;
  if (buffer->ref_count == 0) {
    result = munmap(buffer->vaddr, hnd->size);
    if (result) {
      ALOGE("Unable to unmap buffer (offset: %d, size: %d): %s",
            hnd->offset,
            hnd->size,
            strerror(errno));
    }
    buffer->vaddr = NULL;
  }
  return result;
}
