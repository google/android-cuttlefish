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

#include "cuttlefish/io/lz4_legacy.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cuttlefish/io/filesystem.h"
#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

static constexpr std::string_view kTestFile = "filename.lz4";

TEST(Lz4LegacyTest, RoundTrip) {
  std::string original_data =
      "This is some test data to compress and decompress.";
  static constexpr int kTimesToDuplicate = 10;
  for (int i = 0; i < kTimesToDuplicate; ++i) {
    original_data += original_data;
  }

  std::unique_ptr<ReadWriteFilesystem> fs = InMemoryFilesystem();

  Result<std::unique_ptr<ReaderWriterSeeker>> writer_sink =
      fs->CreateFile(kTestFile);
  ASSERT_THAT(writer_sink, IsOk());
  Result<std::unique_ptr<Writer>> writer =
      Lz4LegacyWriter(std::move(*writer_sink));
  ASSERT_THAT(writer, IsOk());

  EXPECT_THAT((*writer)->Write(original_data.data(), original_data.size()),
              IsOkAndValue(original_data.size()));

  Result<std::unique_ptr<ReaderSeeker>> reader_source =
      fs->OpenReadOnly(kTestFile);
  ASSERT_THAT(reader_source, IsOk());
  Result<std::unique_ptr<Reader>> reader =
      Lz4LegacyReader(std::move(*reader_source));
  ASSERT_THAT(reader, IsOk());

  Result<std::string> decompressed_data = ReadToString(**reader);
  EXPECT_THAT(decompressed_data, IsOkAndValue(original_data));
}

TEST(Lz4LegacyTest, MultipleBlocks) {
  static constexpr size_t kTotalSize = 10 << 20;
  const std::string original_data(kTotalSize, 'a');

  std::unique_ptr<ReadWriteFilesystem> fs = InMemoryFilesystem();

  Result<std::unique_ptr<ReaderWriterSeeker>> writer_sink =
      fs->CreateFile(kTestFile);
  ASSERT_THAT(writer_sink, IsOk());
  Result<std::unique_ptr<Writer>> writer =
      Lz4LegacyWriter(std::move(*writer_sink));
  ASSERT_THAT(writer, IsOk());

  EXPECT_THAT(WriteExact(**writer, original_data.data(), original_data.size()),
              IsOk());

  Result<std::unique_ptr<ReaderSeeker>> reader_source =
      fs->OpenReadOnly(kTestFile);
  ASSERT_THAT(reader_source, IsOk());
  Result<std::unique_ptr<Reader>> reader =
      Lz4LegacyReader(std::move(*reader_source));
  ASSERT_THAT(reader, IsOk());

  Result<std::string> decompressed_data = ReadToString(**reader);
  EXPECT_THAT(decompressed_data, IsOkAndValue(original_data));
}

TEST(Lz4LegacyTest, RejectsWriteAfterFooter) {
  Result<std::unique_ptr<Writer>> writer = Lz4LegacyWriter(InMemoryIo());
  ASSERT_THAT(writer, IsOk());

  // A small write triggers the footer
  std::string data = "some data";
  EXPECT_THAT((*writer)->Write(data.data(), data.size()),
              IsOkAndValue(data.size()));

  // Future writes should fail
  EXPECT_THAT((*writer)->Write(data.data(), data.size()), IsError());
}

TEST(Lz4LegacyTest, ExactlyBlockSizeTriggersFooter) {
  std::unique_ptr<ReadWriteFilesystem> fs = InMemoryFilesystem();

  Result<std::unique_ptr<ReaderWriterSeeker>> writer_sink =
      fs->CreateFile(kTestFile);
  ASSERT_THAT(writer_sink, IsOk());
  Result<std::unique_ptr<Writer>> writer =
      Lz4LegacyWriter(std::move(*writer_sink));
  ASSERT_THAT(writer, IsOk());

  std::string data(kLz4LegacyFrameBlockSize, 'x');
  EXPECT_THAT((*writer)->Write(data.data(), data.size()),
              IsOkAndValue(kLz4LegacyFrameBlockSize));

  // It should be closed now; try adding a bit more.
  EXPECT_THAT((*writer)->Write("more", 4), IsError());

  // Verify it can be read back
  Result<std::unique_ptr<ReaderSeeker>> reader_source =
      fs->OpenReadOnly(kTestFile);
  ASSERT_THAT(reader_source, IsOk());
  Result<std::unique_ptr<Reader>> reader =
      Lz4LegacyReader(std::move(*reader_source));
  ASSERT_THAT(reader, IsOk());

  Result<std::string> decompressed_data = ReadToString(**reader);
  EXPECT_THAT(decompressed_data, IsOkAndValue(data));
}

}  // namespace
}  // namespace cuttlefish
