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

#include "cuttlefish/files/copy_with_attributes.h"

#include <errno.h>
#include <sys/stat.h>

#include <string>

#include "cuttlefish/files/copy.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> CopyWithAttributes(const std::string& from,
                                const std::string& to) {
  CF_EXPECTF(Copy(from, to), "Failed to copy '{}' to '{}'", from, to);
  struct stat st;
  CF_EXPECTF(stat(from.c_str(), &st) >= 0, "Failed to stat '{}': {}", from,
             StrError(errno));
  CF_EXPECTF(chmod(to.c_str(), st.st_mode) >= 0, "Failed to chmod '{}': {}", to,
             StrError(errno));
  return {};
}

}  // namespace cuttlefish
