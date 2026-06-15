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
#include "cuttlefish/posix/readlink.h"

#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::string> ReadLink(const char* path) {
  std::vector<char> buf(4096);
  while (true) {
    ssize_t size = readlink(path, buf.data(), buf.size());
    CF_EXPECTF(size != -1, "readlink('{}') failed: {}", path, StrError(errno));
    if (static_cast<size_t>(size) < buf.size()) {
      return std::string(buf.data(), size);
    }
    buf.resize(buf.size() * 2);
  }
}

Result<std::string> ReadLink(const std::string& path) {
  return ReadLink(path.c_str());
}

Result<std::string> ReadLink(std::string_view path) {
  return ReadLink(std::string(path));
}

}  // namespace cuttlefish
