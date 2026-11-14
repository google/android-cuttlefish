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

#include "cuttlefish/posix/remove_file_if_exists.h"

#include <errno.h>
#include <stdio.h>

#include <string>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void, int> RemoveFileIfExists(const char* file) {
  VLOG(0) << "Removing file if exists " << file;
  CF_EXPECTVF(remove(file) == 0 || errno == ENOENT, errno,
              "remove('{}') error: {}", file, StrError());
  return {};
}

Result<void, int> RemoveFileIfExists(const std::string& file) {
  CF_EXPECT(RemoveFileIfExists(file.c_str()));
  return {};
}

Result<void, int> RemoveFileIfExists(absl::string_view file) {
  CF_EXPECT(RemoveFileIfExists(std::string(file)));
  return {};
}

}  // namespace cuttlefish
