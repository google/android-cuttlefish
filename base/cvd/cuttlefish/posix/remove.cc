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

#include "cuttlefish/posix/remove.h"

#include <errno.h>
#include <stdio.h>

#include <string>
#include <string_view>

#include "absl/log/log.h"

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> RemoveFile(const char* file) {
  VLOG(0) << "Removing file " << file;
  CF_EXPECTF(remove(file) == 0, "remove('{}) error: {}", file, StrError(errno));
  return {};
}

Result<void> RemoveFile(const std::string& file) {
  CF_EXPECT(RemoveFile(file.c_str()));
  return {};
}

Result<void> RemoveFile(std::string_view file) {
  CF_EXPECT(RemoveFile(std::string(file)));
  return {};
}

}  // namespace cuttlefish
