/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include "cuttlefish/posix/symlink.h"

#include <errno.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void, int> Symlink(const char* target, const char* linkpath) {
  CF_EXPECTVF(symlink(target, linkpath) >= 0, errno,
              "symlink('{}', '{}') failed: {}", target, linkpath,
              StrError(errno));
  return {};
}

Result<void, int> Symlink(const std::string& target,
                          const std::string& linkpath) {
  CF_EXPECT(Symlink(target.c_str(), linkpath.c_str()));
  return {};
}

Result<void, int> Symlink(std::string_view target, std::string_view linkpath) {
  CF_EXPECT(Symlink(std::string(target), std::string(linkpath)));
  return {};
}

}  // namespace cuttlefish
