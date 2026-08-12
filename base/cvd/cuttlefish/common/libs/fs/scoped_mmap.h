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

#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_FS_SCOPED_MMAP_H
#define CUTTLEFISH_COMMON_COMMON_LIBS_FS_SCOPED_MMAP_H

#include <stddef.h>
#include <sys/mman.h>

namespace cuttlefish {

// Provides RAII semantics for memory mappings, preventing memory leaks. It does
// not however prevent use-after-free errors since the underlying pointer can be
// extracted and could survive this object.
class ScopedMMap {
 public:
  ScopedMMap();
  ScopedMMap(void* ptr, size_t len);
  ScopedMMap(const ScopedMMap& other) = delete;
  ScopedMMap& operator=(const ScopedMMap& other) = delete;
  ScopedMMap(ScopedMMap&& other);

  ~ScopedMMap();

  void* get() { return ptr_; }
  const void* get() const { return ptr_; }
  size_t len() const { return len_; }

  operator bool() const { return ptr_ != MAP_FAILED; }

  // Checks whether the interval [offset, offset + length) is contained within
  // [0, len_)
  bool WithinBounds(size_t offset, size_t length) const {
    // Don't add offset + len to avoid overflow
    return offset < len_ && len_ - offset >= length;
  }

 private:
  void* ptr_ = MAP_FAILED;
  size_t len_;
};

}  // namespace cuttlefish

#endif  // CUTTLEFISH_COMMON_COMMON_LIBS_FS_SCOPED_MMAP_H
