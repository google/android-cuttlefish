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

#include "cuttlefish/common/libs/fs/scoped_mmap.h"

#include <stddef.h>
#include <sys/mman.h>

namespace cuttlefish {

ScopedMMap::ScopedMMap(void* ptr, size_t len) : ptr_(ptr), len_(len) {}

ScopedMMap::ScopedMMap() : ptr_(MAP_FAILED), len_(0) {}

ScopedMMap::ScopedMMap(ScopedMMap&& other)
    : ptr_(other.ptr_), len_(other.len_) {
  other.ptr_ = MAP_FAILED;
  other.len_ = 0;
}

ScopedMMap::~ScopedMMap() {
  if (ptr_ != MAP_FAILED) {
    munmap(ptr_, len_);
  }
}

}  // namespace cuttlefish
