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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/logcat.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

TEST(LogcatTest, ParseLogcatLineValid) {
  std::string line = "05-15 15:28:15.123  1000  1000 I TagName: message";
  auto parsed = ParseLogcatLine(line);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_EQ(parsed->date, "05-15");
  EXPECT_EQ(parsed->time, "15:28:15.123");
  EXPECT_EQ(parsed->uid, "1000");
  EXPECT_EQ(parsed->pid, "1000");
  EXPECT_EQ(parsed->verbosity, 'I');
  EXPECT_EQ(parsed->tag, "TagName:");
  EXPECT_EQ(parsed->message, "message");
}

TEST(LogcatTest, ParseLogcatLineNoColon) {
  std::string line = "05-15 15:28:15.123  1000  1000 I message without colon";
  auto parsed = ParseLogcatLine(line);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_EQ(parsed->date, "05-15");
  EXPECT_EQ(parsed->time, "15:28:15.123");
  EXPECT_EQ(parsed->uid, "1000");
  EXPECT_EQ(parsed->pid, "1000");
  EXPECT_EQ(parsed->verbosity, 'I');
  EXPECT_EQ(parsed->tag, "");
  EXPECT_EQ(parsed->message, "message without colon");
}

TEST(LogcatTest, ParseLogcatLineInvalid) {
  EXPECT_THAT(ParseLogcatLine("Failed to read logcat:"), IsError());
  EXPECT_THAT(ParseLogcatLine("short line"), IsError());
}

}  // namespace cuttlefish
