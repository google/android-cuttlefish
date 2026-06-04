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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/kernel.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

TEST(KernelTest, ParseKernelLineValid) {
  std::string line = "[    0.123456] init: starting service";
  auto parsed = ParseKernelLine(line);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_EQ(parsed->timestamp, "[    0.123456]");
  EXPECT_EQ(parsed->prefix, " init:");
  EXPECT_EQ(parsed->message, " starting service");
}

TEST(KernelTest, ParseKernelLineNoColon) {
  std::string line = "[    0.123456] Linux version 6.1.0";
  auto parsed = ParseKernelLine(line);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_EQ(parsed->timestamp, "[    0.123456]");
  EXPECT_EQ(parsed->prefix, "");
  EXPECT_EQ(parsed->message, " Linux version 6.1.0");
}

TEST(KernelTest, ParseKernelLineParens) {
  std::string line = "[    0.123456] driver(param:val): message";
  auto parsed = ParseKernelLine(line);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_EQ(parsed->timestamp, "[    0.123456]");
  EXPECT_EQ(parsed->prefix, " driver(param:val):");
  EXPECT_EQ(parsed->message, " message");
}

TEST(KernelTest, ParseKernelLineInvalid) {
  EXPECT_THAT(ParseKernelLine("Failed to read kernel.log:"), IsError());
}

}  // namespace cuttlefish
