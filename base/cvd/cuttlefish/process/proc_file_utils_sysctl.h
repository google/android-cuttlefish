/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <errno.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <cstddef>
#include <span>
#include <vector>

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Reads the sysctl at mib into a vector of T, trimmed to what the kernel
// wrote. With max_size == 0 the kernel is asked for the size first.
template <typename T>
Result<std::vector<T>> ReadSysctl(std::span<const int> mib,
                                  size_t max_size = 0) {
#ifdef __APPLE__
  // macOS declares the name parameter as non-const.
  int* name = const_cast<int*>(mib.data());
#else
  const int* name = mib.data();
#endif
  u_int namelen = mib.size();
  size_t size = max_size;
  if (size == 0) {
    CF_EXPECTF(sysctl(name, namelen, nullptr, &size, nullptr, 0) == 0,
               "sysctl failed: {}", StrError(errno));
  }

  std::vector<T> out(size / sizeof(T));
  if (out.empty()) {
    return out;
  }

  size = out.size() * sizeof(T);
  CF_EXPECTF(sysctl(name, namelen, out.data(), &size, nullptr, 0) == 0,
             "sysctl failed: {}", StrError(errno));
  out.resize(size / sizeof(T));
  return out;
}

}  // namespace cuttlefish
