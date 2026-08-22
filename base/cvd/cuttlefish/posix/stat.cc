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

#include "cuttlefish/posix/stat.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/posix/temp_failure_retry.h"  // IWYU pragma: keep
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<struct stat> Stat(const char* path) {
  struct stat ret;
  int success = TEMP_FAILURE_RETRY(stat(path, &ret));
  CF_EXPECTF(success == 0, "Stat('{}') failed: ", path, StrError(errno));
  return ret;
}

Result<struct stat> Stat(const std::string& path) {
  return CF_EXPECT(Stat(path.c_str()));
}

Result<struct stat> Stat(std::string_view path) {
  return CF_EXPECT(Stat(std::string(path)));
}

}  // namespace cuttlefish
