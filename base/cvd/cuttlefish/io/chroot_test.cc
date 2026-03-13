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

#include "cuttlefish/io/chroot.h"

#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/io/copy.h"
#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(ChrootTest, CreateFileOutsideChroot) {
  std::unique_ptr<ReadWriteFilesystem> real_filesystem = InMemoryFilesystem();
  ASSERT_NE(real_filesystem.get(), nullptr);

  Result<std::unique_ptr<ReaderWriterSeeker>> file_a =
      real_filesystem->CreateFile("/my_dir/file_a");
  ASSERT_THAT(file_a, IsOk());
  ASSERT_THAT(Copy(*InMemoryIo("data"), **file_a), IsOk());

  ChrootReadWriteFilesystem chroot(*real_filesystem, "/my_dir");
  Result<std::unique_ptr<ReaderWriterSeeker>> chroot_a =
      chroot.OpenReadWrite("/file_a");
  ASSERT_THAT(chroot_a, IsOk());
  ASSERT_THAT(ReadToString(**chroot_a), IsOkAndValue("data"));
}

TEST(ChrootTest, CreateFileInsideChroot) {
  std::unique_ptr<ReadWriteFilesystem> real_filesystem = InMemoryFilesystem();
  ASSERT_NE(real_filesystem.get(), nullptr);

  ChrootReadWriteFilesystem chroot(*real_filesystem, "/my_dir");
  Result<std::unique_ptr<ReaderWriterSeeker>> chroot_b =
      chroot.CreateFile("/file_b");
  ASSERT_THAT(Copy(*InMemoryIo("data"), **chroot_b), IsOk());
  ASSERT_THAT(chroot_b, IsOk());

  Result<std::unique_ptr<ReaderWriterSeeker>> file_b =
      real_filesystem->OpenReadWrite("/my_dir/file_b");
  ASSERT_THAT(file_b, IsOk());
  ASSERT_THAT(ReadToString(**file_b), IsOkAndValue("data"));
}

TEST(ChrootTest, PathsRestrainedToPrefix) {
  std::unique_ptr<ReadWriteFilesystem> real_filesystem = InMemoryFilesystem();
  ASSERT_NE(real_filesystem.get(), nullptr);

  Result<std::unique_ptr<ReaderWriterSeeker>> file_a =
      real_filesystem->CreateFile("/my_dir/file_a");
  ASSERT_THAT(file_a, IsOk());
  ASSERT_THAT(Copy(*InMemoryIo("data"), **file_a), IsOk());

  ChrootReadWriteFilesystem chroot(*real_filesystem, "/my_dir");
  Result<std::unique_ptr<ReaderWriterSeeker>> chroot_a =
      chroot.OpenReadWrite("/../../.././file_a");
  ASSERT_THAT(chroot_a, IsOk());
  ASSERT_THAT(ReadToString(**chroot_a), IsOkAndValue("data"));
}

}  // namespace
}  // namespace cuttlefish
