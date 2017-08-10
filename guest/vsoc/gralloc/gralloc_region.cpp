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

#include "gralloc_region.h"

#include <atomic>
#include <common/vsoc/lib/lock_guard.h>
#include <log/log.h>
#include <sys/types.h>
#include <uapi/vsoc_shm.h>

using vsoc::gralloc::GrallocRegion;
using vsoc::layout::gralloc::BufferEntry;
using vsoc::layout::gralloc::GrallocBufferLayout;
using vsoc::layout::gralloc::GrallocManagerLayout;

namespace {
template <typename T>
inline T gralloc_align(T val) {
  return (val + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

// Use the thread id to identify the original creator of a buffer.
inline uint32_t gralloc_owned_value() {
  return gettid();
}

}  // namespace

GrallocRegion::GrallocRegion() {
  // The construction in the singleton is thread safe, so we call Open here to
  // make sure it opens thread safe too. The singleton will return null if the
  // region failed to open.
  Open();
}

bool GrallocRegion::Open(const char* domain) {
  if (is_open_) {
    return true;
  }
  if (!vsoc::ManagerRegionView<GrallocManagerLayout>::Open()) {
    return false;
  }
  std::shared_ptr<vsoc::RegionControl> managed_region =
      vsoc::RegionControl::Open(
          GrallocManagerLayout::ManagedRegion::region_name, domain);
  if (!managed_region) {
    LOG_FATAL("Unable to open managed region");
    return false;
  }
  offset_of_buffer_memory_ = gralloc_align<vsoc_reg_off_t>(
      managed_region->region_desc().offset_of_region_data);
  total_buffer_memory_ =
      managed_region->region_size() - offset_of_buffer_memory_;

  // TODO(jemoreira): Handle the case of unexpected values in the region.
  is_open_ = true;
  return true;
}

int GrallocRegion::AllocateBuffer(size_t size, uint32_t* begin_offset) {
  size = gralloc_align<size_t>(size);
  // Cache the value of buffer_count in shared memory.
  uint32_t buffer_count_local = 0;
  {
    vsoc::LockGuard<vsoc::layout::GuestLock>(&data()->new_buffer_lock);
    buffer_count_local = data()->buffer_count;
  }
  // Find a free buffer entry of the appropriate size.
  for (uint32_t idx = 0; idx < buffer_count_local; ++idx) {
    BufferEntry& entry = data()->buffers_table[idx];
    if (entry.owned_by == VSOC_REGION_FREE && entry.buffer_size() == size) {
      int fd = control_->CreateFdScopedPermission(
          GrallocManagerLayout::ManagedRegion::region_name,
          pointer_to_region_offset(&entry.owned_by),
          gralloc_owned_value(),
          entry.buffer_begin,
          entry.buffer_end);
      if (fd >= 0) {
        if (begin_offset) {
          *begin_offset = entry.buffer_begin;
        }
        return fd;
      }
    }
  }

  // We couldn't find any suitable buffer, create one
  {
    vsoc::LockGuard<vsoc::layout::GuestLock>(&data()->new_buffer_lock);
    // don't use the cached value here!!!
    uint32_t idx = data()->buffer_count;
    if (pointer_to_region_offset(&data()->buffers_table[idx + 1]) >
        control_->region_size()) {
      ALOGE(
          "Out of memory in gralloc_manager (total: %d, used: %d, "
          "requested: %d)",
          control_->region_size(),
          pointer_to_region_offset(&data()->buffers_table[idx]),
          sizeof(data()->buffers_table[idx]));
      return -ENOMEM;
    }
    if (total_buffer_memory_ - data()->allocated_buffer_memory < size) {
      ALOGE(
          "Out of memory in gralloc_memory (total: %d, used: %d, requested: %d)",
          total_buffer_memory_,
          data()->allocated_buffer_memory,
          size);
      return -ENOMEM;
    }
    // Initialize the buffer entry and acquire ownership
    // Do it before increasing buffer_count so that another thread looking for
    // free entries doesn't find this one
    BufferEntry& new_entry = data()->buffers_table[idx];
    new_entry.buffer_begin =
        offset_of_buffer_memory_ + data()->allocated_buffer_memory;
    data()->allocated_buffer_memory += size;
    new_entry.buffer_end = new_entry.buffer_begin + size;
    int fd = control_->CreateFdScopedPermission(
        GrallocManagerLayout::ManagedRegion::region_name,
        pointer_to_region_offset(&new_entry.owned_by),
        gralloc_owned_value(),
        new_entry.buffer_begin,
        new_entry.buffer_end);
    if (fd < 0) {
      LOG_FATAL(
          "Unexpected error while creating fd scoped permission over "
          "uncontested memory: %s",
          strerror(-fd));
      return fd;
    }
    // Increment buffer_count now that the entry can't be taken from us
    data()->buffer_count++;
    if (begin_offset) {
      *begin_offset = new_entry.buffer_begin;
    }
    return fd;
  }
}

/* static */
// The C++03 standard does not guarantee this singleton implemention to be
// thread safe, however magic statics are part of the gcc compiler since
// version 4.3.
GrallocRegion* GrallocRegion::GetInstance() {
  static GrallocRegion singleton;
  if (!singleton.is_open_) {
    return NULL;
  }
  return &singleton;
}
