//
// Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/libs/zip/buffered_zip_source.h"

#include <stddef.h>
#include <stdint.h>

#include <utility>
#include <vector>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

// For testing interactions with partial buffer reads.
constexpr size_t kPrimeUnderlyingDataSize = 23;
constexpr size_t kPrimeBufferSize = 7;
constexpr size_t kPrimeReadSize = 3;

// For testing interactions with complete buffer reads.
constexpr size_t kCommonUnderlyingDataSize = 24;
constexpr size_t kCommonBufferSize = 8;
constexpr size_t kCommonReadSize = 4;

TEST(BufferedZipSourceTest, ManySmallReads) {
  std::vector<uint8_t> data_in;
  for (int i = 0; i < kPrimeUnderlyingDataSize; i++) {
    data_in.push_back(i);
  }
  Result<WritableZipSource> data_source =
      WritableZipSource::BorrowData(data_in.data(), data_in.size());
  ASSERT_THAT(data_source, IsOk());

  Result<SeekableZipSource> buffered =
      BufferZipSource(std::move(*data_source), kPrimeBufferSize);
  ASSERT_THAT(buffered, IsOk());

  Result<SeekingZipSourceReader> reader = buffered->Reader();
  ASSERT_THAT(reader, IsOk());

  std::vector<uint8_t> data_out(data_in.size() + kPrimeReadSize);
  size_t read_offset = 0;
  while (true) {
    uint8_t* ptr = &data_out[read_offset];
    Result<size_t> amount_read = reader->Read(ptr, kPrimeReadSize);
    ASSERT_THAT(amount_read, IsOk());
    if (*amount_read == 0) {
      break;
    }
    read_offset += *amount_read;
  }
  data_out.resize(read_offset);
  EXPECT_EQ(data_in, data_out);
}

// Worst case for triggering re-reads
TEST(BufferedZipSourceTest, ReadBackwards) {
  std::vector<uint8_t> data_in;
  for (int i = 0; i < kCommonUnderlyingDataSize; i++) {
    data_in.push_back(i);
  }
  Result<WritableZipSource> data_source =
      WritableZipSource::BorrowData(data_in.data(), data_in.size());
  ASSERT_THAT(data_source, IsOk());

  Result<SeekableZipSource> buffered =
      BufferZipSource(std::move(*data_source), kCommonBufferSize);
  ASSERT_THAT(buffered, IsOk());

  Result<SeekingZipSourceReader> reader = buffered->Reader();
  ASSERT_THAT(reader, IsOk());

  std::vector<uint8_t> data_out(data_in.size());
  size_t read_offset = data_out.size() - kCommonReadSize;
  while (true) {
    ASSERT_THAT(reader->SeekSet(read_offset), IsOk());
    uint8_t* ptr = &data_out[read_offset];
    Result<size_t> amount_read = reader->Read(ptr, kCommonReadSize);
    ASSERT_THAT(amount_read, IsOkAndValue(kCommonReadSize));
    if (read_offset == 0) {
      break;
    }
    read_offset -= kCommonReadSize;
  }
  EXPECT_EQ(data_in, data_out);
}

TEST(BufferedZipSourceTest, ManySmallReadsInMemoryIo) {
  std::vector<char> data_in;
  for (int i = 0; i < kPrimeUnderlyingDataSize; i++) {
    data_in.push_back(i);
  }
  Result<WritableZipSource> data_source =
      WritableZipSource::BorrowData(data_in.data(), data_in.size());
  ASSERT_THAT(data_source, IsOk());

  Result<SeekableZipSource> buffered =
      BufferZipSource(InMemoryIo(data_in), kPrimeBufferSize);
  ASSERT_THAT(buffered, IsOk());

  Result<SeekingZipSourceReader> reader = buffered->Reader();
  ASSERT_THAT(reader, IsOk());

  std::vector<char> data_out(data_in.size() + kPrimeReadSize);
  size_t read_offset = 0;
  while (true) {
    char* ptr = &data_out[read_offset];
    Result<size_t> amount_read = reader->Read(ptr, kPrimeReadSize);
    ASSERT_THAT(amount_read, IsOk());
    if (*amount_read == 0) {
      break;
    }
    read_offset += *amount_read;
  }
  data_out.resize(read_offset);
  EXPECT_EQ(data_in, data_out);
}

}  // namespace
}  // namespace cuttlefish
