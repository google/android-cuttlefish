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

#include "cuttlefish/common/libs/utils/disk_usage.h"

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <set>
#include <string>
#include <utility>

#include "cuttlefish/files/directory_contents.h"
#include "cuttlefish/posix/stat.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

using InodeSet = std::set<std::pair<dev_t, ino_t>>;

Result<size_t> DirectoryApparentSize(const std::string& dir,
                                     const struct stat& dir_st,
                                     InodeSet& counted_inodes) {
  size_t total = static_cast<size_t>(dir_st.st_size);
  for (const std::string& name : CF_EXPECT(DirectoryContents(dir))) {
    const std::string entry = dir + "/" + name;
    const struct stat st = CF_EXPECT(Lstat(entry));
    if (S_ISDIR(st.st_mode)) {
      total += CF_EXPECT(DirectoryApparentSize(entry, st, counted_inodes));
      continue;
    }
    // Hard links show up once per name; count each inode once.
    if (S_ISREG(st.st_mode) && st.st_nlink > 1 &&
        !counted_inodes.insert({st.st_dev, st.st_ino}).second) {
      continue;
    }
    total += static_cast<size_t>(st.st_size);
  }
  return total;
}

Result<size_t> ApparentSize(const std::string& path) {
  const struct stat st = CF_EXPECT(Stat(path));
  if (S_ISREG(st.st_mode)) {
    return static_cast<size_t>(st.st_size);
  }
  CF_EXPECTF(S_ISDIR(st.st_mode),
             "\"{}\" is neither a regular file nor a directory", path);
  InodeSet counted_inodes;
  return CF_EXPECT(DirectoryApparentSize(path, st, counted_inodes));
}

}  // namespace

Result<size_t> GetDiskUsageBytes(const std::string& path) {
  return CF_EXPECTF(ApparentSize(path),
                    "Unable to determine disk usage of file \"{}\"", path);
}

Result<size_t> GetDiskUsageGigabytes(const std::string& path) {
  size_t bytes =
      CF_EXPECTF(ApparentSize(path),
                 "Unable to determine disk usage of file \"{}\"", path);
  static constexpr size_t kGigabyte = size_t{1} << 30;
  return (bytes + kGigabyte - 1) / kGigabyte;
}

}  // namespace cuttlefish
