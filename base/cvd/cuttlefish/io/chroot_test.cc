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

#include <memory>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/io/copy.h"
#include "cuttlefish/io/in_memory_fs.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/owning_in_memory_io.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(ChrootTest, CreateFileOutsideChroot) {
  InMemoryFilesystem base_filesystem;

  std::unique_ptr<ReaderWriterSeeker> file_a =
      CF_ASSERT(base_filesystem.CreateFile("/my_dir/file_a"));
  ASSERT_THAT(Copy(OwningInMemoryIo("data"), *file_a), IsOk());

  ChrootReadWriteFilesystem chroot(base_filesystem, "/my_dir");
  std::unique_ptr<ReaderWriterSeeker> chroot_a =
      CF_ASSERT(chroot.OpenReadWrite("/file_a"));

  ASSERT_THAT(ReadToString(*chroot_a), IsOkAndValue("data"));
}

TEST(ChrootTest, CreateFileInsideChroot) {
  InMemoryFilesystem base_filesystem;

  ChrootReadWriteFilesystem chroot(base_filesystem, "/my_dir");
  std::unique_ptr<ReaderWriterSeeker> chroot_b =
      CF_ASSERT(chroot.CreateFile("/file_b"));
  ASSERT_THAT(Copy(OwningInMemoryIo("data"), *chroot_b), IsOk());

  std::unique_ptr<ReaderWriterSeeker> file_b =
      CF_ASSERT(base_filesystem.OpenReadWrite("/my_dir/file_b"));

  ASSERT_THAT(ReadToString(*file_b), IsOkAndValue("data"));
}

TEST(ChrootTest, PathsRestrainedToPrefix) {
  InMemoryFilesystem base_filesystem;

  std::unique_ptr<ReaderWriterSeeker> file_a =
      CF_ASSERT(base_filesystem.CreateFile("/my_dir/file_a"));
  ASSERT_THAT(Copy(OwningInMemoryIo("data"), *file_a), IsOk());

  ChrootReadWriteFilesystem chroot(base_filesystem, "/my_dir");
  std::unique_ptr<ReaderWriterSeeker> chroot_a =
      CF_ASSERT(chroot.OpenReadWrite("/../../.././file_a"));

  ASSERT_THAT(ReadToString(*chroot_a), IsOkAndValue("data"));
}

}  // namespace
}  // namespace cuttlefish
