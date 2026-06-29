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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/display.h"

#include <cstddef>
#include <memory>
#include <string>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/io.h"

namespace cuttlefish {

TEST(LogMonitorDisplayTest, DrawFileValid) {
  std::unique_ptr<ReaderWriterSeeker> rs = InMemoryIo("line1\nline2\nline3\n");
  ASSERT_TRUE(rs.get());

  LogMonitorDisplay display(40);
  display.DrawFile(*rs, "test.log");

  std::string output = display.Finalize().output;
  EXPECT_TRUE(absl::StrContains(output, "+--test.log "));
  EXPECT_TRUE(
      absl::StrContains(output, "|line1                                 |"));
  EXPECT_TRUE(
      absl::StrContains(output, "|line2                                 |"));
  EXPECT_TRUE(
      absl::StrContains(output, "|line3                                 |"));
  EXPECT_TRUE(
      absl::StrContains(output, "+--------------------------------------+"));
}

TEST(LogMonitorDisplayTest, TotalLinesDrawn) {
  std::unique_ptr<ReaderWriterSeeker> rs = InMemoryIo("line1\n");
  ASSERT_TRUE(rs.get());

  LogMonitorDisplay display(40);

  display.DrawFile(*rs, "test.log");
  int total_lines_drawn = display.Finalize().total_lines_drawn;
  // 10 lines of content + 1 top border + 1 bottom border
  EXPECT_EQ(total_lines_drawn, 12);
}

TEST(LogMonitorDisplayTest, DrawFileLastNLinesOrder) {
  std::string data;
  for (int i = 1; i <= 12; ++i) {
    data += absl::StrCat("line", i, "\n");
  }
  std::unique_ptr<ReaderWriterSeeker> rs = InMemoryIo(data);
  ASSERT_TRUE(rs.get());

  LogMonitorDisplay display(40);
  display.DrawFile(*rs, "test.log");

  std::string output = display.Finalize().output;

  // Should contain line3 to line12
  for (int i = 3; i <= 12; ++i) {
    EXPECT_TRUE(absl::StrContains(output, absl::StrCat("|line", i)));
  }
  // Should NOT contain line1 or line2
  EXPECT_FALSE(absl::StrContains(output, "|line1 "));
  EXPECT_FALSE(absl::StrContains(output, "|line2 "));

  // Check order
  size_t last_pos = 0;
  for (int i = 3; i <= 12; ++i) {
    size_t pos = output.find(absl::StrCat("|line", i));
    ASSERT_NE(pos, std::string::npos);
    EXPECT_GT(pos, last_pos);
    last_pos = pos;
  }
}

TEST(LogMonitorDisplayTest, DrawFileLinesAcrossChunks) {
  std::string data;
  for (int i = 1; i <= 10; ++i) {
    std::string line = absl::StrCat("Line ", i, ": ");
    int fill_len = 500 - line.length() - 1;
    line += std::string(fill_len, 'A' + i);
    line += "\n";
    data += line;
  }
  ASSERT_EQ(data.size(), 5000);

  std::unique_ptr<ReaderWriterSeeker> rs = InMemoryIo(data);
  ASSERT_TRUE(rs.get());

  LogMonitorDisplay display(80);
  display.DrawFile(*rs, "test.log");

  std::string output = display.Finalize().output;

  // Verify all lines are present
  for (int i = 1; i <= 10; ++i) {
    EXPECT_TRUE(absl::StrContains(output, absl::StrCat("Line ", i, ": ")));
  }
}

}  // namespace cuttlefish
