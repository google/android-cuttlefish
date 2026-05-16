/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/truncate.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/ansi_codes.h"

namespace cuttlefish {

TEST(TruncateColoredStringTest, NoAnsiNoTruncate) {
  auto [str, len] = TruncateColoredString("hello", 10);
  EXPECT_EQ(str, "hello");
  EXPECT_EQ(len, 5);
}

TEST(TruncateColoredStringTest, NoAnsiTruncate) {
  auto [str, len] = TruncateColoredString("hello world", 5);
  // Should NOT append reset if not in input
  EXPECT_EQ(str, "hello");
  EXPECT_EQ(len, 5);
}

TEST(TruncateColoredStringTest, AnsiNoTruncate) {
  std::string input = absl::StrCat(kAnsiRed, "hello", kAnsiReset);
  auto [str, len] = TruncateColoredString(input, 10);
  EXPECT_EQ(len, 5);
  EXPECT_EQ(str, absl::StrCat(kAnsiRed, "hello", kAnsiReset));
}

TEST(TruncateColoredStringTest, AnsiTruncate) {
  std::string input = absl::StrCat(kAnsiRed, "hello world", kAnsiReset);
  auto [str, len] = TruncateColoredString(input, 5);
  EXPECT_EQ(len, 5);
  // Should preserve the reset from the end of the input
  EXPECT_EQ(str, absl::StrCat(kAnsiRed, "hello", kAnsiReset));
}

TEST(TruncateColoredStringTest, AnsiTruncateNoResetInInput) {
  std::string input = absl::StrCat(kAnsiRed, "hello world");
  auto [str, len] = TruncateColoredString(input, 5);
  EXPECT_EQ(len, 5);
  // Should NOT append reset if not in input
  EXPECT_EQ(str, absl::StrCat(kAnsiRed, "hello"));
}

}  // namespace cuttlefish
