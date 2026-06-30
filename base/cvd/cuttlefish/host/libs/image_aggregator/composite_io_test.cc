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

#include "cuttlefish/host/libs/image_aggregator/composite_io.h"

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include "cuttlefish/io/io.h"
#include "cuttlefish/host/libs/image_aggregator/composite_disk.h"
#include "cuttlefish/io/filesystem.h"
#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result_matchers.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

TEST(CompositeIoTest, SeekValue) {
  std::unique_ptr<ReadWriteFilesystem> fs = InMemoryFilesystem();
  ASSERT_NE(fs.get(), nullptr);

  static constexpr std::string_view kAPath = "a";
  static constexpr std::string_view kAContent = "hello ";
  Result<std::unique_ptr<ReaderWriterSeeker>> file_a = fs->CreateFile(kAPath);
  ASSERT_THAT(file_a, IsOk());
  ASSERT_NE(file_a->get(), nullptr);
  ASSERT_THAT(WriteString(**file_a, kAContent), IsOk());

  static constexpr std::string_view kBPath = "b";
  static constexpr std::string_view kBContent = "world";
  Result<std::unique_ptr<ReaderWriterSeeker>> file_b = fs->CreateFile(kBPath);
  ASSERT_THAT(file_b, IsOk());
  ASSERT_NE(file_b->get(), nullptr);
  ASSERT_THAT(WriteString(**file_b, kBContent), IsOk());

  CompositeDisk disk;
  disk.set_length(kAContent.size() + kBContent.size());

  ComponentDisk& member_a = *disk.add_component_disks();
  member_a.set_file_path(kAPath);
  member_a.set_offset(0);

  ComponentDisk& member_b = *disk.add_component_disks();
  member_b.set_file_path(kBPath);
  member_b.set_offset(kAContent.size());

  CompositeDiskImage image(std::move(disk));

  Result<CompositeDiskReaderIo> io =
      CompositeDiskReaderIo::Create(std::move(image), *fs);
  ASSERT_THAT(io, IsOk());

  ASSERT_THAT(ReadToString(*io),
              IsOkAndValue(absl::StrCat(kAContent, kBContent)));
}

}  // namespace
}  // namespace cuttlefish
