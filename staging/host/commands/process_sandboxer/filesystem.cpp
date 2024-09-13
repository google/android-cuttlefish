// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/process_sandboxer/filesystem.h"

#include <sys/stat.h>

#include <deque>
#include <initializer_list>
#include <string>
#include <string_view>

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <absl/strings/str_split.h>
#include <absl/strings/strip.h>

namespace cuttlefish::process_sandboxer {

// Copied from sandboxed_api/util/path.cc

namespace internal {

constexpr char kPathSeparator[] = "/";

std::string JoinPathImpl(std::initializer_list<absl::string_view> paths) {
  std::string result;
  for (const auto& path : paths) {
    if (path.empty()) {
      continue;
    }
    if (result.empty()) {
      absl::StrAppend(&result, path);
      continue;
    }
    const auto comp = absl::StripPrefix(path, kPathSeparator);
    if (absl::EndsWith(result, kPathSeparator)) {
      absl::StrAppend(&result, comp);
    } else {
      absl::StrAppend(&result, kPathSeparator, comp);
    }
  }
  return result;
}

}  // namespace internal

// Copied from sandboxed_api/util/fileops.cc

namespace {

std::string StripBasename(std::string_view path) {
  const auto last_slash = path.find_last_of('/');
  if (last_slash == std::string::npos) {
    return "";
  }
  if (last_slash == 0) {
    return "/";
  }
  return std::string(path.substr(0, last_slash));
}

}  // namespace

bool CreateDirectoryRecursively(const std::string& path, int mode) {
  if (mkdir(path.c_str(), mode) == 0 || errno == EEXIST) {
    return true;
  }

  // We couldn't create the dir for reasons we can't handle.
  if (errno != ENOENT) {
    return false;
  }

  // The ENOENT case, the parent directory doesn't exist yet.
  // Let's create it.
  const std::string dir = StripBasename(path);
  if (dir == "/" || dir.empty()) {
    return false;
  }
  if (!CreateDirectoryRecursively(dir, mode)) {
    return false;
  }

  // Now the parent dir exists, retry creating the directory.
  return mkdir(path.c_str(), mode) == 0;
}

std::string CleanPath(const std::string_view unclean_path) {
  int dotdot_num = 0;
  std::deque<absl::string_view> parts;
  for (absl::string_view part :
       absl::StrSplit(unclean_path, '/', absl::SkipEmpty())) {
    if (part == "..") {
      if (parts.empty()) {
        ++dotdot_num;
      } else {
        parts.pop_back();
      }
    } else if (part != ".") {
      parts.push_back(part);
    }
  }
  if (absl::StartsWith(unclean_path, "/")) {
    if (parts.empty()) {
      return "/";
    }
    parts.push_front("");
  } else {
    for (; dotdot_num; --dotdot_num) {
      parts.push_front("..");
    }
    if (parts.empty()) {
      return ".";
    }
  }
  return absl::StrJoin(parts, "/");
}

}  // namespace cuttlefish::process_sandboxer
