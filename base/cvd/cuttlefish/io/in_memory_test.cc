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

#include "cuttlefish/io/in_memory.h"

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

TEST(InMemoryIoTest, WriteSeek) {
  InMemoryIo instance;

  constexpr std::string_view str = "hello";
  ASSERT_THAT(instance.PartialWrite(str.data(), str.size()),
              IsOkAndValue(str.size()));

  ASSERT_THAT(instance.SeekCur(-2), IsOkAndValue(str.size() - 2));
  ASSERT_THAT(instance.SeekCur(-2), IsOkAndValue(str.size() - 4));
  ASSERT_THAT(instance.SeekEnd(-2), IsOkAndValue(str.size() - 2));
}

TEST(InMemoryIoTest, WriteSeekRead) {
  InMemoryIo instance;

  constexpr std::string_view str = "hello";
  ASSERT_THAT(instance.PartialWrite(str.data(), str.size()),
              IsOkAndValue(str.size()));

  ASSERT_THAT(instance.SeekSet(0), IsOkAndValue(0));

  std::string data_read(str.size(), '\0');
  ASSERT_THAT(instance.PartialRead(data_read.data(), str.size()),
              IsOkAndValue(str.size()));

  ASSERT_EQ(str, data_read);
}

TEST(InMemoryIoTest, WriteAtReadAt) {
  InMemoryIo instance;

  constexpr std::string_view str = "hello";
  ASSERT_THAT(instance.PartialWriteAt(str.data(), str.size(), 2),
              IsOkAndValue(str.size()));

  std::string data_read(str.size() + 1, '\0');
  ASSERT_THAT(instance.PartialReadAt(data_read.data(), str.size() + 1, 1),
              IsOkAndValue(str.size() + 1));

  ASSERT_EQ(absl::StrCat(std::string_view("\0", 1), str), data_read);
}

TEST(InMemoryIoTest, WriteWriteReadAt) {
  InMemoryIo instance;

  constexpr std::string_view str = "hello";
  ASSERT_THAT(instance.PartialWrite(str.data(), str.size()),
              IsOkAndValue(str.size()));
  ASSERT_THAT(instance.PartialWrite(str.data(), str.size()),
              IsOkAndValue(str.size()));

  std::string data_read(str.size() * 2, '\0');
  ASSERT_THAT(instance.PartialReadAt(data_read.data(), data_read.size(), 0),
              IsOkAndValue(data_read.size()));

  ASSERT_EQ(absl::StrCat(str, str), data_read);
}

}  // namespace cuttlefish
