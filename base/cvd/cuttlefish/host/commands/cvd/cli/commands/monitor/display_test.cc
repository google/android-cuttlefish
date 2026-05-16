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
#include <string>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"

namespace cuttlefish {

TEST(LogMonitorDisplayTest, DrawFileValid) {
  SharedFD fd =
      SharedFD::MemfdCreateWithData("test_log", "line1\nline2\nline3\n");
  ASSERT_TRUE(fd->IsOpen());

  LogMonitorDisplay display(40);
  display.DrawFile(fd, "test.log");

  std::string output = display.Finalize();
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

TEST(LogMonitorDisplayTest, DrawFileInvalid) {
  SharedFD fd;  // Invalid FD

  LogMonitorDisplay display(40);
  display.DrawFile(fd, "test.log");

  std::string output = display.Finalize();
  EXPECT_TRUE(absl::StrContains(output, "+--test.log "));
  EXPECT_TRUE(
      absl::StrContains(output, "|Failed to read test.log: File not open|"));
  EXPECT_TRUE(
      absl::StrContains(output, "+--------------------------------------+"));
}

TEST(LogMonitorDisplayTest, TotalLinesDrawn) {
  SharedFD fd = SharedFD::MemfdCreateWithData("test_log", "line1\n");
  LogMonitorDisplay display(40);

  EXPECT_EQ(display.TotalLinesDrawn(), 0);

  display.DrawFile(fd, "test.log");
  EXPECT_EQ(display.TotalLinesDrawn(),
            11);  // 10 lines of content + 1 top border

  display.Finalize();
  EXPECT_EQ(display.TotalLinesDrawn(), 12);  // +1 bottom border
}

TEST(LogMonitorDisplayTest, DrawFileLastNLinesOrder) {
  std::string data;
  for (int i = 1; i <= 12; ++i) {
    data += absl::StrCat("line", i, "\n");
  }
  SharedFD fd = SharedFD::MemfdCreateWithData("test_log", data);
  ASSERT_TRUE(fd->IsOpen());

  LogMonitorDisplay display(40);
  display.DrawFile(fd, "test.log");

  std::string output = display.Finalize();

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

  SharedFD fd = SharedFD::MemfdCreateWithData("test_log", data);
  ASSERT_TRUE(fd->IsOpen());

  LogMonitorDisplay display(80);
  display.DrawFile(fd, "test.log");

  std::string output = display.Finalize();

  // Verify all lines are present
  for (int i = 1; i <= 10; ++i) {
    EXPECT_TRUE(absl::StrContains(output, absl::StrCat("Line ", i, ": ")));
  }
}

}  // namespace cuttlefish
