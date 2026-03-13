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

#include "cuttlefish/io/read_window_view.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result_matchers.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

TEST(ReadWindowViewTest, ReadString) {
  std::unique_ptr<ReaderSeeker> underlying = InMemoryIo("(hello)");
  ReadWindowView window = ReadWindowView(*underlying, 1, 5);

  EXPECT_THAT(ReadToString(window), IsOkAndValue("hello"));
}

TEST(ReadWindowViewTest, ReadStringAfterSeekPointerMoves) {
  std::unique_ptr<ReaderSeeker> underlying = InMemoryIo("(hello)");
  ReadWindowView window = ReadWindowView(*underlying, 1, 5);

  EXPECT_THAT(window.SeekSet(2), IsOkAndValue(2));
  EXPECT_THAT(ReadToString(window), IsOkAndValue("llo"));
}

}  // namespace
}  // namespace cuttlefish
