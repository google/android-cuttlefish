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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/log_tee.h"

#include <string>

#include "gtest/gtest.h"

namespace cuttlefish {

TEST(LogTeeTest, ParseLogTeeLineValid) {
  std::string line =
      "[2026-05-15T23:43:46.448645816+00:00 INFO  disk] disk size 294518784";
  auto parsed = ParseLogTeeLine(line);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed->date, "[2026-05-15T23:43:46.448645816+00:00");
  EXPECT_EQ(parsed->verbosity, "INFO");
  EXPECT_EQ(parsed->subsystem, "disk]");
  EXPECT_EQ(parsed->message, " disk size 294518784");
}

TEST(LogTeeTest, ParseLogTeeLineInvalid) {
  EXPECT_FALSE(ParseLogTeeLine("Failed to read launcher.log:").ok());
}

TEST(LogTeeTest, FormatLogTeeLinePlain) {
  LogTeeLine line{
      .date = "[2026-05-15T23:43:46.448645816+00:00",
      .verbosity = "INFO",
      .subsystem = "disk]",
      .message = " disk size 294518784",
  };
  std::string out = FormatLogTeeLine(line, false, 70);
  EXPECT_EQ(
      out,
      "[2026-05-15T23:43:46.448645816+00:00 INFO disk] disk size 294518784   ");
}

}  // namespace cuttlefish
