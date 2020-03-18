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

#include <android-base/logging.h>

#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

namespace cvd {

bool FileExists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

bool FileHasContent(const std::string& path) {
  return FileSize(path) > 0;
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

bool IsDirectoryEmpty(const std::string& path) {
  auto direc = ::opendir(path.c_str());
  if (!direc) {
    LOG(ERROR) << "IsDirectoryEmpty test failed with " << path
               << " as it failed to be open" << std::endl;
    return false;
  }

  decltype(::readdir(direc)) sub = nullptr;
  int cnt {0};
  while ( (sub = ::readdir(direc)) ) {
    cnt++;
    if (cnt > 2) {
    LOG(ERROR) << "IsDirectoryEmpty test failed with " << path
               << " as it exists but not empty" << std::endl;
      return false;
    }
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

off_t FileSize(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) == -1) {
    return 0;
  }
  return st.st_size;
}

// TODO(schuffelen): Use std::filesystem::last_write_time when on C++17
std::chrono::system_clock::time_point FileModificationTime(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) == -1) {
    return std::chrono::system_clock::time_point();
  }
  std::chrono::seconds seconds(st.st_mtim.tv_sec);
  return std::chrono::system_clock::time_point(seconds);
}

bool RemoveFile(const std::string& file) {
  LOG(INFO) << "Removing " << file;
  return remove(file.c_str()) == 0;
}

std::string CurrentDirectory() {
  char* path = getcwd(nullptr, 0);
  std::string ret(path);
  free(path);
  return ret;
}

}  // namespace cvd
