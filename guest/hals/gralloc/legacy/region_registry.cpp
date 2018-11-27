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

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "VSoCGrallocRegionRegistry"

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

#include <linux/ashmem.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>

#include "gralloc_vsoc_priv.h"

#include <deque>
#include <map>
#include <mutex>

// TODO(ghartman): Make the configurable through a property
static const bool g_log_refs = false;

struct GrallocRegion {
  void* base_;
  int   num_references_;

  GrallocRegion() : base_(0), num_references_(0) { }
  // Copy constructors are ok.
};


static const char* get_buffer_name(
    const private_handle_t* hnd, char output[ASHMEM_NAME_LEN]) {
  output[0] = '\0';
  if (!hnd) {
    ALOGE("Attempted to log gralloc name hnd=NULL");
    return output;
  }
  if (hnd->fd == -1) {
    ALOGE("Attempted to log gralloc name hnd=%p with fd == -1", hnd);
    return output;
  }
  int rval = ioctl(hnd->fd, ASHMEM_GET_NAME, output);
  if (rval == -1) {
    output[0] = '\0';
  }
  return output;
}


static int str_hash(void* str) {
  return hashmapHash(str, strlen(reinterpret_cast<const char*>(str)));
}


static bool str_equal(void* a, void* b) {
  return strcmp(
      reinterpret_cast<const char*>(a),
      reinterpret_cast<const char*>(b)) == 0;
}


static Hashmap* get_regions() {
  static Hashmap* regionMap = hashmapCreate(19, str_hash, str_equal);
  return regionMap;
}


static GrallocRegion* lock_region_for_handle(
    const private_handle_t* hnd, char region_name[ASHMEM_NAME_LEN]) {
  region_name[0] = '\0';
  get_buffer_name(hnd, region_name);
  Hashmap* hash = get_regions();
  hashmapLock(hash);
  GrallocRegion* region = reinterpret_cast<GrallocRegion*>(
      hashmapGet(hash, region_name));
  if (!region) {
    region = new GrallocRegion;
    hashmapPut(hash, strdup(region_name), region);
  }
  return region;
}


/* The current implementation uses only a single lock for all regions.
 * This method takes a region to simplfy the refactoring if we go to
 * finer-grained locks.
 */
static inline void unlock_region(GrallocRegion* ) {
  hashmapUnlock(get_regions());
}


/*
 * surface_flinger can drop its last reference to a gralloc buffer (from the
 * gralloc HAL's point of view) even though it also has work in flight to the
 * GPU for that target. This causes segfaults in the swiftshader code.
 *
 * We create a compromise solution. On unmap we release the pages by mmaping
 * anonymous memory over the range, but we don't release the address space.
 * Instead we mark the address space for recycling into a new gralloc buffer.
 * This means that the shaders can still write, that the writes won't land in
 * the gralloc buffer, and the gralloc buffer memory can be released.
 *
 * When we're preparing to mmap a new gralloc buffer we see if we can recycle
 * address space from a prior gralloc buffer.
 *
 * The protects the application layer from stray memory writes and pointer
 * references to freed memory. It does mean that bad pixel data can land in
 * a buffer in the case of a fast map-unmap-map sequence. However, that
 * could also happen on a physical GPU.
 *
 * The alternative to this would be to create an elaborate reference counting
 * mechanism below both gralloc and SwiftShader. However, we want to keep the
 * SwiftShader code clean, so that seems undesirable.
 *
 * This problem also comes up for physical GPUs b/62267886. Background fo rthis
 * solution is in b/118777601
 */

static std::map<size_t, std::deque<void*>> g_recycled_addrs;
std::mutex g_recycled_addrs_mutex;



static void* recycle_mmap(void *addr, size_t length, int prot, int flags,
                          int fd, off_t offset) {
  if (!addr) {
    std::lock_guard<std::mutex> guard(g_recycled_addrs_mutex);
    auto it = g_recycled_addrs.find(length);
    if (it != g_recycled_addrs.end()) {
      if (it->second.size()) {
        addr = it->second.front();
        flags |= MAP_FIXED;
        it->second.pop_front();
      }
    }
  }
  return mmap(addr, length, prot, flags, fd, offset);
}


static int recycle_munmap(void *addr, size_t length) {
  // Do this first so we don't hold the mutex during the syscall
  if (addr != mmap(addr, length, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0)) {
    // Be conservative. Don't recycle here.
    return -1;
  }
  std::lock_guard<std::mutex> guard(g_recycled_addrs_mutex);
  g_recycled_addrs[length].push_back(addr);
  return 0;
}


void* reference_region(const char* op, const private_handle_t* hnd) {
  char name_buf[ASHMEM_NAME_LEN];
  GrallocRegion* region = lock_region_for_handle(hnd, name_buf);
  if (!region->base_) {
    void* mappedAddress = recycle_mmap(
        0, hnd->total_size, PROT_READ|PROT_WRITE, MAP_SHARED, hnd->fd, 0);
    if (mappedAddress == MAP_FAILED) {
      ALOGE("Could not mmap %s", strerror(errno));
      unlock_region(region);
      return NULL;
    }
    // Set up the guard pages. The last page is always a guard
    uintptr_t base = uintptr_t(mappedAddress);
    uintptr_t addr = base + hnd->total_size - PAGE_SIZE;
    if (mprotect((void*)addr, PAGE_SIZE, PROT_NONE) == -1) {
      ALOGE("mprotect base=%p, pg=%p failed (%s)", (void*)base, (void*)addr,
            strerror(errno));
    }
    region->base_ = mappedAddress;
    ALOGI("Mapped %s hnd=%p fd=%d base=%p format=%s(0x%x) width=%d height=%d",
          name_buf, hnd, hnd->fd, region->base_,
          pixel_format_to_string(hnd->format), hnd->format,
          hnd->x_res, hnd->y_res);
  }

  void* rval = region->base_;
  ++region->num_references_;
  ALOGI_IF(g_log_refs, "Referencing name=%s op=%s addr=%p new numRefs=%d",
        name_buf, op, region->base_, region->num_references_);
  unlock_region(region);
  return rval;
}


int unreference_region(const char* op, const private_handle_t* hnd) {
  char name_buf[ASHMEM_NAME_LEN];

  GrallocRegion* region = lock_region_for_handle(hnd, name_buf);
  if (!region->base_) {
    ALOGE("Unmapping region with no map hnd=%p", hnd);
    unlock_region(region);
    return -1;
  }
  if (region->num_references_ < 1) {
    ALOGE(
        "unmap with hnd=%p, numReferences=%d", hnd, region->num_references_);
    unlock_region(region);
    return -1;
  }
  --region->num_references_;
  if (!region->num_references_) {
    ALOGI("Unmapped %s hnd=%p fd=%d base=%p", name_buf, hnd,
          hnd->fd, region->base_);
    if (recycle_munmap(region->base_, hnd->total_size) < 0) {
      ALOGE("Could not unmap %s", strerror(errno));
    }
    region->base_ = 0;
  }
  ALOGI_IF(g_log_refs, "Unreferencing name=%s op=%s addr=%p new numRefs=%d",
        name_buf, op, region->base_, region->num_references_);
  unlock_region(region);
  return 0;
}
