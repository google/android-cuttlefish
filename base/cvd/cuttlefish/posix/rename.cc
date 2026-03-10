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
#include "cuttlefish/posix/rename.h"

#include <string>
#include <string_view>

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> Rename(const char* oldpath, const char* newpath) {
  if (rename(oldpath, newpath) < 0) {
    return CF_ERRF("rename('{}', '{}') failed: {}", oldpath, newpath,
                   StrError(errno));
  }
  return {};
}

Result<void> Rename(const std::string& oldpath, const std::string& newpath) {
  CF_EXPECT(Rename(oldpath.c_str(), newpath.c_str()));
  return {};
}

Result<void> Rename(std::string_view oldpath, std::string_view newpath) {
  CF_EXPECT(Rename(std::string(oldpath), std::string(newpath)));
  return {};
}

}  // namespace cuttlefish
