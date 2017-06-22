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

// Object that represents a region on the Host

#include <atomic>
#include <cstdint>
#include <stdlib.h>
#include <sys/mman.h>

#include "common/libs/fs/shared_fd.h"
#include "uapi/vsoc_shm.h"

namespace vsoc {

/**
 * Base class to access a region in VSoC shared memory.
 * This class holds the methods that do not depend on the type to simplify
 * the template expansions.
 *
 * This should not be directly instantiated. While technically concrete
 * a resonable implementation needs additional methods:
 *
 *   * Knowledge of the region's layout (see RegionSingleton)
 *   * Guest or host-specific code to gain access to the region
 */
class RegionBase {
 public:
  virtual ~RegionBase();

  uint32_t region_size() const {
    return region_desc_.region_end_offset - region_desc_.region_begin_offset;
  }

  uint32_t region_data_size() const {
    return region_size() - region_desc_.offset_of_region_data;
  }

 protected:
  RegionBase() {}

  template <typename T> T* region_offset_to_pointer(uint32_t offset) {
    return reinterpret_cast<T*> (
        reinterpret_cast<uintptr_t>(region_base_) +
        offset - region_desc_.region_begin_offset);
  }

  template <typename T> uint32_t pointer_to_region_offset(T* ptr) {
    return reinterpret_cast<uintptr_t>(ptr) - region_desc_.region_begin_offset;
  }

  vsoc_device_region region_desc_{};
  void *region_base_{};
};

}  // vsoc
