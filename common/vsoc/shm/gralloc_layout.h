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

#include "common/vsoc/shm/base.h"
#include "common/vsoc/shm/graphics.h"
#include "common/vsoc/shm/lock.h"
#include "common/vsoc/shm/version.h"
#include "uapi/vsoc_shm.h"

// Memory layout for the gralloc manager region.

namespace vsoc {
namespace layout {

namespace gralloc {

struct BufferEntry {
  uint32_t owned_by;
  vsoc_reg_off_t buffer_begin;
  vsoc_reg_off_t buffer_end;

  PixelFormatRegister pixel_format;
  uint32_t stride;
  uint32_t width;
  uint32_t height;

  // A size of 28 is causing different layouts when GrallocManagerLayout is
  // compiled in host and guest sides
  uint32_t padding;

  uint32_t buffer_size() {
    return buffer_end - buffer_begin;
  }
};
ASSERT_SHM_COMPATIBLE(BufferEntry, gralloc);

struct GrallocBufferLayout : public RegionLayout {
  static const char* region_name;
};
ASSERT_SHM_COMPATIBLE(GrallocBufferLayout, gralloc);

struct GrallocManagerLayout : public RegionLayout {
  static const char* region_name;
  typedef GrallocBufferLayout ManagedRegion;

  uint32_t allocated_buffer_memory;
  uint32_t buffer_count;
  // Make sure this isn't the first field
  GuestLock new_buffer_lock;
  // Needs to be last field
  BufferEntry buffers_table[1];
};
ASSERT_SHM_COMPATIBLE(GrallocManagerLayout, gralloc);

} // namespace gralloc
} // namespace layout
} // namespace vsoc
