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

#include "cuttlefish/io/concat.h"

#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(ConcatTest, ConcatThreeStrings) {
  std::vector<std::unique_ptr<ReaderSeeker>> members;
  members.emplace_back(InMemoryIo("hello"));
  members.emplace_back(InMemoryIo(" "));
  members.emplace_back(InMemoryIo("world"));

  Result<ConcatReaderSeeker> concatenated =
      ConcatReaderSeeker::Create(std::move(members));
  ASSERT_THAT(concatenated, IsOk());

  ASSERT_THAT(ReadToString(*concatenated), IsOkAndValue("hello world"));
}

TEST(ConcatTest, CannotBeEmpty) {
  ASSERT_THAT(ConcatReaderSeeker::Create({}), IsError());
}

TEST(ConcatTest, CannotHaveNulls) {
  std::vector<std::unique_ptr<ReaderSeeker>> members;
  members.emplace_back(InMemoryIo("hello"));
  members.emplace_back(nullptr);

  ASSERT_THAT(ConcatReaderSeeker::Create(std::move(members)), IsError());
}

}  // namespace
}  // namespace cuttlefish
