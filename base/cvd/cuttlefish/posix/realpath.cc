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
#include <linux/limits.h>
#include <stdlib.h>

#include <array>
#include <string>

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::string> RealPath(const std::string& path) {
  std::array<char, PATH_MAX> buffer{};
  char* res;
  do {
    res = realpath(path.c_str(), buffer.data());
  } while (res == nullptr && errno == EINTR);
  CF_EXPECTF(res != nullptr, "Could not get real path for path \"{}\": {}",
             path, StrError(errno));
  return std::string(buffer.data());
}

}  // namespace cuttlefish
