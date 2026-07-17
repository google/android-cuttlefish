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

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor_source.h"

namespace cuttlefish {
namespace {

TEST(LogMonitorDisplayTest, LineCountMatches) {
  MonitorOutput monitor_output("test1.log", {"line1", "line2", "line3"});

  LogMonitorDisplay display(20);
  display.DrawReport(std::move(monitor_output), 3);

  const LogMonitorDisplayResult result = display.Finalize();
  ASSERT_EQ(result.total_lines_drawn, 5);

  const std::vector<std::string> lines = absl::StrSplit(result.output, '\n');
  ASSERT_EQ(lines.size(), 6);

  EXPECT_EQ(lines[0], "+--test1.log ------+");
  EXPECT_EQ(lines[1], "|line1             |");
  EXPECT_EQ(lines[2], "|line2             |");
  EXPECT_EQ(lines[3], "|line3             |");
  EXPECT_EQ(lines[4], "+------------------+");
  EXPECT_EQ(lines[5], "");
}

TEST(LogMonitorDisplayTest, TooFewLines) {
  MonitorOutput monitor_output("test2.log", {"line1"});

  LogMonitorDisplay display(20);
  display.DrawReport(std::move(monitor_output), 3);

  const LogMonitorDisplayResult result = display.Finalize();
  ASSERT_EQ(result.total_lines_drawn, 5);

  const std::vector<std::string> lines = absl::StrSplit(result.output, '\n');
  ASSERT_EQ(lines.size(), 6);

  EXPECT_EQ(lines[0], "+--test2.log ------+");
  EXPECT_EQ(lines[1], "|line1             |");
  EXPECT_EQ(lines[2], "|                  |");
  EXPECT_EQ(lines[3], "|                  |");
  EXPECT_EQ(lines[4], "+------------------+");
  EXPECT_EQ(lines[5], "");
}

TEST(LogMonitorDisplayTest, TooManyLines) {
  MonitorOutput monitor_output("test3.log",
                               {"line1", "line2", "line3", "line4"});

  LogMonitorDisplay display(20);
  display.DrawReport(monitor_output, 2);

  const LogMonitorDisplayResult result = display.Finalize();
  ASSERT_EQ(result.total_lines_drawn, 4);

  const std::vector<std::string> lines = absl::StrSplit(result.output, '\n');
  ASSERT_EQ(lines.size(), 5);

  EXPECT_EQ(lines[0], "+--test3.log ------+");
  EXPECT_EQ(lines[1], "|line3             |");
  EXPECT_EQ(lines[2], "|line4             |");
  EXPECT_EQ(lines[3], "+------------------+");
  EXPECT_EQ(lines[4], "");
}

TEST(LogMonitorDisplayTest, LinesTooLong) {
  MonitorOutput monitor_output("test4.log", {"line1AAAAAAAAAAAAAAAAAAAAAAAAA"});

  LogMonitorDisplay display(20);
  display.DrawReport(monitor_output, 2);

  const LogMonitorDisplayResult result = display.Finalize();
  ASSERT_EQ(result.total_lines_drawn, 4);

  const std::vector<std::string> lines = absl::StrSplit(result.output, '\n');
  ASSERT_EQ(lines.size(), 5);

  EXPECT_EQ(lines[0], "+--test4.log ------+");
  EXPECT_EQ(lines[1], "|line1AAAAAAAAAAAAA|");
  EXPECT_EQ(lines[2], "|                  |");
  EXPECT_EQ(lines[3], "+------------------+");
  EXPECT_EQ(lines[4], "");
}

}  // namespace
}  // namespace cuttlefish
