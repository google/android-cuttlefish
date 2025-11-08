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

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/result_matchers.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source.h"

namespace cuttlefish {
namespace {

TEST(BufferedZipSourceTest, ManySmallReads) {
  std::vector<uint8_t> data_in;
  for (int i = 0; i < 23; i++) {
    data_in.push_back(i);
  }
  Result<WritableZipSource> data_source =
      WritableZipSource::BorrowData(data_in.data(), data_in.size());
  ASSERT_THAT(data_source, IsOk());

  Result<SeekableZipSource> buffered =
      BufferZipSource(std::move(*data_source), 7);
  ASSERT_THAT(buffered, IsOk());

  Result<SeekingZipSourceReader> reader = buffered->Reader();
  ASSERT_THAT(reader, IsOk());

  std::vector<uint8_t> data_out(data_in.size() + 3);
  size_t read_offset = 0;
  while (true) {
    uint8_t* ptr = &data_out[read_offset];
    Result<size_t> amount_read = reader->Read(ptr, 3);
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
  for (int i = 0; i < 24; i++) {
    data_in.push_back(i);
  }
  Result<WritableZipSource> data_source =
      WritableZipSource::BorrowData(data_in.data(), data_in.size());
  ASSERT_THAT(data_source, IsOk());

  Result<SeekableZipSource> buffered =
      BufferZipSource(std::move(*data_source), 8);
  ASSERT_THAT(buffered, IsOk());

  Result<SeekingZipSourceReader> reader = buffered->Reader();
  ASSERT_THAT(reader, IsOk());

  std::vector<uint8_t> data_out(data_in.size());
  size_t read_offset = data_out.size() - 4;
  while (true) {
    ASSERT_THAT(reader->SeekFromStart(read_offset), IsOk());
    uint8_t* ptr = &data_out[read_offset];
    Result<size_t> amount_read = reader->Read(ptr, 4);
    ASSERT_THAT(amount_read, IsOkAndValue(4));
    if (read_offset == 0) {
      break;
    }
    read_offset -= 4;
  }
  EXPECT_EQ(data_in, data_out);
}

}  // namespace
}  // namespace cuttlefish
