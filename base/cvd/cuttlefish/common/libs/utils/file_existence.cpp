/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/file_existence.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

namespace cuttlefish {

bool FileExists(const std::string& path, bool follow_symlinks) {
  struct stat st{};
  return (follow_symlinks ? stat : lstat)(path.c_str(), &st) == 0;
}

bool DirectoryExists(const std::string& path, bool follow_symlinks) {
  struct stat st{};
  if ((follow_symlinks ? stat : lstat)(path.c_str(), &st) == -1) {
    return false;
  }
  if ((st.st_mode & S_IFMT) != S_IFDIR) {
    return false;
  }
  return true;
}

}  // namespace cuttlefish
