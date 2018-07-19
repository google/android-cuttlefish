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

#include "common/libs/utils/files.h"

#include <glog/logging.h>

#include <array>
#include <climits>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cvd {

bool FileHasContent(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) == -1) {
    return false;
  }
  if (st.st_size == 0) {
    return false;
  }
  return true;
}

bool DirectoryExists(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) == -1) {
    return false;
  }
  if ((st.st_mode & S_IFMT) != S_IFDIR) {
    return false;
  }
  return true;
}

std::string AbsolutePath(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  if (path[0] == '/') {
    return path;
  }

  std::array<char, PATH_MAX> buffer{};
  if (!realpath(".", buffer.data())) {
    LOG(WARNING) << "Could not get real path for current directory \".\""
                 << ": " << strerror(errno);
    return {};
  }
  return std::string{buffer.data()} + "/" + path;
}

}  // namespace cvd
