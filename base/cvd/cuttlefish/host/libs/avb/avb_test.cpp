//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/libs/avb/avb.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <string>

#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/directory_contents.h"
#include "cuttlefish/files/is_symlink.h"
#include "cuttlefish/posix/stat.h"
#include "cuttlefish/posix/symlink.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::UnorderedElementsAre;

constexpr size_t kVbMetaMaxSize = 65536;

class EnforceVbMetaSizeTest : public ::testing::Test {
 protected:
  std::string Path(const std::string& name) const {
    return std::string(dir_.path) + "/" + name;
  }

  static std::string Padded(const std::string& contents) {
    return contents + std::string(kVbMetaMaxSize - contents.size(), '\0');
  }

  TemporaryDir dir_;
};

TEST_F(EnforceVbMetaSizeTest, PadsPrivateFileInPlace) {
  const std::string vbmeta = Path("vbmeta.img");
  const std::string contents(4096, 'v');
  ASSERT_THAT(WriteNewFile(vbmeta, contents), IsOk());
  const ino_t inode_before = CF_ASSERT(Stat(vbmeta)).st_ino;

  ASSERT_THAT(EnforceVbMetaSize(vbmeta), IsOk());

  EXPECT_THAT(ReadFileContents(vbmeta), IsOkAndValue(Padded(contents)));
  EXPECT_EQ(CF_ASSERT(Stat(vbmeta)).st_ino, inode_before);
}

// b/571068789: `vbmeta.img` may be hard linked to a download cache entry,
// which must not be modified.
TEST_F(EnforceVbMetaSizeTest, DoesNotModifyHardLinkedFile) {
  const std::string cache_blob = Path("cache_blob");
  const std::string vbmeta = Path("vbmeta.img");
  const std::string contents(4096, 'v');
  ASSERT_THAT(WriteNewFile(cache_blob, contents), IsOk());
  ASSERT_EQ(link(cache_blob.c_str(), vbmeta.c_str()), 0);

  ASSERT_THAT(EnforceVbMetaSize(vbmeta), IsOk());

  EXPECT_EQ(FileSize(vbmeta), static_cast<off_t>(kVbMetaMaxSize));
  EXPECT_THAT(ReadFileContents(vbmeta), IsOkAndValue(Padded(contents)));
  EXPECT_THAT(ReadFileContents(cache_blob), IsOkAndValue(contents));
  EXPECT_NE(CF_ASSERT(Stat(cache_blob)).st_ino, CF_ASSERT(Stat(vbmeta)).st_ino);
  EXPECT_EQ(CF_ASSERT(Stat(cache_blob)).st_nlink, static_cast<nlink_t>(1));
  EXPECT_EQ(CF_ASSERT(Stat(vbmeta)).st_nlink, static_cast<nlink_t>(1));
}

TEST_F(EnforceVbMetaSizeTest, ReplacesSymlinkWithoutModifyingTarget) {
  const std::string target = Path("target");
  const std::string vbmeta = Path("vbmeta.img");
  const std::string contents(4096, 'v');
  ASSERT_THAT(WriteNewFile(target, contents), IsOk());
  ASSERT_THAT(Symlink(target, vbmeta), IsOk());

  ASSERT_THAT(EnforceVbMetaSize(vbmeta), IsOk());

  EXPECT_THAT(ReadFileContents(target), IsOkAndValue(contents));
  EXPECT_THAT(IsSymlink(vbmeta), IsOkAndValue(false));
  EXPECT_THAT(ReadFileContents(vbmeta), IsOkAndValue(Padded(contents)));
}

TEST_F(EnforceVbMetaSizeTest, LeavesMaxSizeFileUntouched) {
  const std::string cache_blob = Path("cache_blob");
  const std::string vbmeta = Path("vbmeta.img");
  const std::string contents(kVbMetaMaxSize, 'v');
  ASSERT_THAT(WriteNewFile(cache_blob, contents), IsOk());
  ASSERT_EQ(link(cache_blob.c_str(), vbmeta.c_str()), 0);

  ASSERT_THAT(EnforceVbMetaSize(vbmeta), IsOk());

  EXPECT_THAT(ReadFileContents(vbmeta), IsOkAndValue(contents));
  EXPECT_EQ(CF_ASSERT(Stat(cache_blob)).st_ino, CF_ASSERT(Stat(vbmeta)).st_ino);
}

TEST_F(EnforceVbMetaSizeTest, PadsReadOnlyFileByCopy) {
  if (geteuid() == 0) {
    GTEST_SKIP() << "root can open a read-only file for writing";
  }
  const std::string vbmeta = Path("vbmeta.img");
  const std::string contents(4096, 'v');
  constexpr mode_t kReadOnly = S_IRUSR | S_IRGRP | S_IROTH;
  ASSERT_THAT(WriteNewFile(vbmeta, contents, kReadOnly), IsOk());
  const ino_t inode_before = CF_ASSERT(Stat(vbmeta)).st_ino;

  ASSERT_THAT(EnforceVbMetaSize(vbmeta), IsOk());

  EXPECT_THAT(ReadFileContents(vbmeta), IsOkAndValue(Padded(contents)));
  EXPECT_NE(CF_ASSERT(Stat(vbmeta)).st_ino, inode_before);
  EXPECT_EQ(CF_ASSERT(Stat(vbmeta)).st_mode & 07777, kReadOnly);
}

TEST_F(EnforceVbMetaSizeTest, PreservesPermissions) {
  const std::string vbmeta = Path("vbmeta.img");
  ASSERT_THAT(WriteNewFile(vbmeta, "vbmeta", S_IRUSR | S_IWUSR | S_IRGRP),
              IsOk());

  ASSERT_THAT(EnforceVbMetaSize(vbmeta), IsOk());

  EXPECT_EQ(CF_ASSERT(Stat(vbmeta)).st_mode & 07777,
            static_cast<mode_t>(S_IRUSR | S_IWUSR | S_IRGRP));
}

TEST_F(EnforceVbMetaSizeTest, RejectsOversizedFile) {
  const std::string vbmeta = Path("vbmeta.img");
  const std::string contents(kVbMetaMaxSize + 1, 'v');
  ASSERT_THAT(WriteNewFile(vbmeta, contents), IsOk());

  EXPECT_THAT(EnforceVbMetaSize(vbmeta), IsError());

  EXPECT_THAT(ReadFileContents(vbmeta), IsOkAndValue(contents));
}

TEST_F(EnforceVbMetaSizeTest, CopyLeavesNoTemporaryFiles) {
  const std::string cache_blob = Path("cache_blob");
  const std::string vbmeta = Path("vbmeta.img");
  ASSERT_THAT(WriteNewFile(cache_blob, "vbmeta"), IsOk());
  ASSERT_EQ(link(cache_blob.c_str(), vbmeta.c_str()), 0);

  ASSERT_THAT(EnforceVbMetaSize(vbmeta), IsOk());

  EXPECT_THAT(DirectoryContents(dir_.path),
              IsOkAndValue(UnorderedElementsAre("cache_blob", "vbmeta.img")));
}

}  // namespace
}  // namespace cuttlefish
