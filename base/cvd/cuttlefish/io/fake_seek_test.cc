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

#include "cuttlefish/io/fake_seek.h"

#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/result/result_matchers.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

class ReadFromVector : public ReaderFakeSeeker {
 public:
  ReadFromVector(std::vector<char> data)
      : ReaderFakeSeeker(data.size()), data_(data) {}

  Result<uint64_t> PRead(void* buf, uint64_t count,
                         uint64_t offset) const override {
    offset = std::min(data_.size(), offset);
    if (offset + count >= data_.size()) {
      count = data_.size() - offset;
    }
    if (count > 0) {
      memcpy(buf, data_.data() + offset, count);
    }
    return count;
  }

 private:
  std::vector<char> data_;
};

TEST(FakeSeekTest, SequentialReads) {
  ReadFromVector reader({0, 1, 2, 3, 4, 5, 6, 7});

  for (char i = 0; i < 8; i++) {
    char data;
    EXPECT_THAT(reader.Read(&data, 1), IsOkAndValue(1));
    EXPECT_EQ(data, i);
  }
}

TEST(FakeSeekTest, ReadUpdatesSeekPos) {
  ReadFromVector reader({0, 1, 2, 3, 4, 5, 6, 7});

  for (char i = 0; i < 8; i++) {
    char data;
    EXPECT_THAT(reader.Read(&data, 1), IsOk());
    EXPECT_THAT(reader.SeekCur(0), IsOkAndValue(i + 1));
  }
}

TEST(FakeSeekTest, SeekUpdatesPos) {
  ReadFromVector reader({0, 1, 2, 3, 4, 5, 6, 7});

  for (char i = 0; i < 8; i++) {
    EXPECT_THAT(reader.SeekCur(1), IsOkAndValue(i + 1));
  }
}

TEST(FakeSeekTest, SeekEnd) {
  ReadFromVector reader({0, 1, 2, 3, 4, 5, 6, 7});

  EXPECT_THAT(reader.SeekEnd(-1), IsOkAndValue(7));
}

TEST(FakeSeekTest, SeekSet) {
  ReadFromVector reader({0, 1, 2, 3, 4, 5, 6, 7});

  EXPECT_THAT(reader.SeekSet(2), IsOkAndValue(2));
}

}  // namespace
}  // namespace cuttlefish
