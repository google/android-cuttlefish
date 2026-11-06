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

#include "cuttlefish/io/length.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/io/owning_in_memory_io.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(LengthTest, LengthEmpty) {
  ASSERT_THAT(Length(OwningInMemoryIo()), IsOkAndValue(0));
}

TEST(LengthTest, LengthWithData) {
  ASSERT_THAT(Length(OwningInMemoryIo({1, 2, 3})), IsOkAndValue(3));
}

TEST(LengthTest, ResetsSeekPos) {
  OwningInMemoryIo data({1, 2, 3, 4, 5});

  ASSERT_THAT(data.SeekSet(2), IsOkAndValue(2));
  ASSERT_THAT(Length(data), IsOkAndValue(5));
  ASSERT_THAT(data.SeekCur(0), IsOkAndValue(2));
}

}  // namespace
}  // namespace cuttlefish
