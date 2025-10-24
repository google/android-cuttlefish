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
#include "cuttlefish/common/libs/posix/symlink.h"

#include <string>

#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

Result<void> Symlink(const std::string& target, const std::string& linkpath) {
  if (symlink(target.c_str(), linkpath.c_str()) < 0) {
    return CF_ERRF("symlink(\"{}\", \"{}\") failed: {}", target, linkpath,
                   StrError(errno));
  }
  return {};
}

}  // namespace cuttlefish
