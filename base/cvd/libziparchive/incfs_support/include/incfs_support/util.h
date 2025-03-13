/*
 * Copyright (C) 2021 The Android Open Source Project
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

#pragma once

#ifdef __BIONIC__
#include <linux/incrementalfs.h>
#include <sys/vfs.h>
#endif

namespace incfs::util {

// Some tools useful for writing hardened code

// Clear the passed container and make sure it frees all allocated memory.
// Useful for signal handling code where any remaining memory would leak.
template <class Container>
void clearAndFree(Container& c) {
  Container().swap(c);
}

bool isIncfsFd([[maybe_unused]] int fd) {
#ifdef __BIONIC__
  struct statfs fs = {};
  if (::fstatfs(fd, &fs) != 0) {
    return false;
  }
  return fs.f_type == static_cast<decltype(fs.f_type)>(INCFS_MAGIC_NUMBER);
#else
  return false;  // incfs is linux-specific, and only matters on Android.
#endif
}

}  // namespace incfs::util
