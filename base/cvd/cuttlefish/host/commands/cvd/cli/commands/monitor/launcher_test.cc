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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/launcher.h"

#include <string>

#include "gtest/gtest.h"

namespace cuttlefish {

TEST(LauncherTest, ParseLauncherLineValid) {
  std::string line =
      "kernel_log_monitor(4089384)  I 05-15 16:39:26 4089384 4089384 "
      "kernel_log_server.cc:153] VIRTUAL_DEVICE_BOOT_COMPLETED";
  auto parsed = ParseLauncherLine(line);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed->timestamp, "05-15 16:39:26");
  EXPECT_EQ(parsed->ids, " 4089384 4089384");
  EXPECT_EQ(parsed->verbosity, 'I');
  EXPECT_EQ(parsed->proc_name, "kernel_log_monitor");
  EXPECT_EQ(parsed->message, " VIRTUAL_DEVICE_BOOT_COMPLETED");
}

TEST(LauncherTest, ParseLauncherLineInvalid) {
  EXPECT_FALSE(ParseLauncherLine("Failed to read launcher.log:").ok());
}

TEST(LauncherTest, FormatLauncherLinePlain) {
  LauncherLine line{
      .timestamp = "05-15 16:39:26",
      .ids = " 4089384 4089384",
      .verbosity = 'I',
      .proc_name = "kernel_log_monitor",
      .message = " VIRTUAL_DEVICE_BOOT_COMPLETED",
  };
  std::string out = FormatLauncherLine(line, false, 85);
  EXPECT_EQ(out,
            "05-15 16:39:26 4089384 4089384 I kernel_log_monitor: "
            "VIRTUAL_DEVICE_BOOT_COMPLETED   ");
}

}  // namespace cuttlefish
