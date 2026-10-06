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

#include "cuttlefish/common/libs/utils/disk_usage.h"

#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

constexpr size_t kGigabyte = size_t{1} << 30;

std::string WriteFileOfSize(const std::string& dir, std::string_view name,
                            size_t size) {
  std::string path = dir + "/" + std::string(name);
  EXPECT_TRUE(android::base::WriteStringToFile(std::string(size, 'x'), path))
      << path;
  return path;
}

size_t LstatSize(const std::string& path) {
  struct stat st;
  EXPECT_EQ(lstat(path.c_str(), &st), 0) << path;
  return static_cast<size_t>(st.st_size);
}

TEST(DiskUsageTest, RegularFile) {
  constexpr size_t kSize = 12345;
  TemporaryDir dir;
  const std::string file = WriteFileOfSize(dir.path, "file", kSize);

  EXPECT_THAT(GetDiskUsageBytes(file), IsOkAndValue(kSize));
}

TEST(DiskUsageTest, EmptyFile) {
  TemporaryDir dir;
  const std::string file = WriteFileOfSize(dir.path, "empty", 0);

  EXPECT_THAT(GetDiskUsageBytes(file), IsOkAndValue(size_t{0}));
  EXPECT_THAT(GetDiskUsageGigabytes(file), IsOkAndValue(size_t{0}));
}

TEST(DiskUsageTest, GigabytesRoundUp) {
  TemporaryDir dir;
  const std::string small = WriteFileOfSize(dir.path, "small", 1);
  const std::string large = WriteFileOfSize(dir.path, "large", 0);
  // A sparse file: apparent size without the disk usage.
  ASSERT_EQ(truncate(large.c_str(), kGigabyte + 1), 0);

  EXPECT_THAT(GetDiskUsageGigabytes(small), IsOkAndValue(size_t{1}));
  EXPECT_THAT(GetDiskUsageGigabytes(large), IsOkAndValue(size_t{2}));
}

TEST(DiskUsageTest, MissingFileIsAnError) {
  TemporaryDir dir;
  EXPECT_THAT(GetDiskUsageBytes(dir.path + std::string("/missing")), IsError());
}

TEST(DiskUsageTest, FifoIsAnError) {
  TemporaryDir dir;
  const std::string fifo = dir.path + std::string("/fifo");
  ASSERT_EQ(mkfifo(fifo.c_str(), 0600), 0);

  EXPECT_THAT(GetDiskUsageBytes(fifo), IsError());
}

TEST(DiskUsageTest, SymlinkArgumentIsFollowed) {
  constexpr size_t kSize = 12345;
  constexpr std::string_view kName = "file";
  TemporaryDir dir;
  WriteFileOfSize(dir.path, kName, kSize);
  const std::string link = dir.path + std::string("/link");
  ASSERT_EQ(symlink(std::string(kName).c_str(), link.c_str()), 0);

  EXPECT_THAT(GetDiskUsageBytes(link), IsOkAndValue(kSize));
}

TEST(DiskUsageTest, DirectoryIsApparentSizeOfContents) {
  constexpr size_t kSizeA = 1000;
  constexpr size_t kSizeB = 2048;
  TemporaryDir dir;
  const std::string root = dir.path;
  const std::string sub = root + "/sub";
  ASSERT_EQ(mkdir(sub.c_str(), 0755), 0);
  WriteFileOfSize(root, "a", kSizeA);
  WriteFileOfSize(sub, "b", kSizeB);

  // Directory inodes count too; their st_size depends on the filesystem.
  EXPECT_THAT(GetDiskUsageBytes(root),
              IsOkAndValue(kSizeA + kSizeB + LstatSize(root) + LstatSize(sub)));
  EXPECT_THAT(GetDiskUsageBytes(sub), IsOkAndValue(kSizeB + LstatSize(sub)));
}

TEST(DiskUsageTest, HardLinkIsCountedOnce) {
  constexpr size_t kSize = 1000;
  TemporaryDir dir;
  const std::string root = dir.path;
  const std::string file = WriteFileOfSize(root, "a", kSize);
  ASSERT_EQ(link(file.c_str(), (root + "/a_link").c_str()), 0);

  EXPECT_THAT(GetDiskUsageBytes(root), IsOkAndValue(kSize + LstatSize(root)));
}

TEST(DiskUsageTest, SymlinkInDirectoryIsNotFollowed) {
  constexpr size_t kSize = 1000;
  constexpr std::string_view kName = "a";
  TemporaryDir dir;
  const std::string root = dir.path;
  WriteFileOfSize(root, kName, kSize);
  const std::string sym = root + "/a_sym";
  ASSERT_EQ(symlink(std::string(kName).c_str(), sym.c_str()), 0);

  // The symlink contributes the length of its target name, not the target.
  ASSERT_EQ(LstatSize(sym), kName.size());
  EXPECT_THAT(GetDiskUsageBytes(root),
              IsOkAndValue(kSize + LstatSize(sym) + LstatSize(root)));
}

}  // namespace
}  // namespace cuttlefish
