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

#include "cuttlefish/files/file_device_id.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<dev_t> FileDeviceId(const std::string& path) {
  struct stat out;
  CF_EXPECTF(
      stat(path.c_str(), &out) == 0,
      "stat() failed trying to retrieve device ID information for \"{}\" "
      "with error: {}",
      path, StrError(errno));
  return out.st_dev;
}

}  // namespace cuttlefish
