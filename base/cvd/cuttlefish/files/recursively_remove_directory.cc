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

#include "cuttlefish/files/recursively_remove_directory.h"

#include <errno.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "absl/log/log.h"

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> RecursivelyRemoveDirectory(const std::string& path) {
  // Copied from libbase TemporaryDir destructor.
  auto callback = [](const char* child, const struct stat*, int file_type,
                     struct FTW*) -> int {
    switch (file_type) {
      case FTW_D:
      case FTW_DP:
      case FTW_DNR:
        if (rmdir(child) == -1) {
          PLOG(ERROR) << "rmdir " << child;
          return -1;
        }
        break;
      case FTW_NS:
      default:
        if (rmdir(child) != -1) {
          break;
        }
        // FALLTHRU (for gcc, lint, pcc, etc; and following for clang)
        [[fallthrough]];
      case FTW_F:
      case FTW_SL:
      case FTW_SLN:
        if (unlink(child) == -1) {
          PLOG(ERROR) << "unlink " << child;
          return -1;
        }
        break;
    }
    return 0;
  };

  if (nftw(path.c_str(), callback, 128, FTW_DEPTH | FTW_PHYS) < 0) {
    return CF_ERRF("Failed to remove dir '{}': {}", path, StrError(errno));
  }
  return {};
}

}  // namespace cuttlefish
