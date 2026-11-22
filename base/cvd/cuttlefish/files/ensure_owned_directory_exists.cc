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

#include "cuttlefish/files/ensure_owned_directory_exists.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> EnsureOwnedDirectoryExists(const std::string& directory_path,
                                        const mode_t mode) {
  return EnsureOwnedDirectoryExists(directory_path, mode, geteuid());
}

Result<void> EnsureOwnedDirectoryExists(const std::string& directory_path,
                                        const mode_t mode,
                                        const uid_t owner_uid) {
  std::string path = directory_path;
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  const mode_t target_mode = mode & 07777;
  CF_EXPECTF(mkdir(path.c_str(), target_mode) == 0 || errno == EEXIST,
             "Failed to create directory \"{}\": {}", directory_path,
             StrError());
  Fd dir_fd = CF_EXPECTF(
      Fd::Open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC),
      "Failed to open directory \"{}\"", directory_path);
  const struct stat st = CF_EXPECT(dir_fd.Fstat());
  CF_EXPECTF(st.st_uid == owner_uid,
             "Directory \"{}\" is owned by uid {}, expected {}", directory_path,
             st.st_uid, owner_uid);
  if ((st.st_mode & 07777) != target_mode) {
    CF_EXPECTF(dir_fd.Chmod(target_mode),
               "Failed to set permission on \"{}\": {}", directory_path,
               dir_fd.StrError());
  }
  return {};
}

}  // namespace cuttlefish
