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

#include "cuttlefish/posix/realpath.h"

#include <errno.h>
#include <limits.h>
#ifdef __linux__
#include <linux/limits.h>
#endif
#include <stdlib.h>

#include <array>
#include <string>
#include <string_view>

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::string, int> RealPath(const char* path) {
  std::array<char, PATH_MAX> buffer{};
  char* res;
  do {
    res = realpath(path, buffer.data());
  } while (res == nullptr && errno == EINTR);
  CF_EXPECTVF(res != nullptr, errno, "realpath('{}') failed: {}", path,
              StrError(errno));
  return std::string(buffer.data());
}

Result<std::string, int> RealPath(const std::string& path) {
  return CF_EXPECT(RealPath(path.c_str()));
}

Result<std::string, int> RealPath(std::string_view path) {
  return CF_EXPECT(RealPath(std::string(path)));
}

}  // namespace cuttlefish
