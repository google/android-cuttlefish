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

#include "cuttlefish/host/commands/cvd/cli/log_files.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(LogFilesTest, PruneLogsDirectory) {
  const char* tmpdir = getenv("TEST_TMPDIR");
  ASSERT_NE(tmpdir, nullptr);
  std::string log_dir = std::string(tmpdir) + "/logs_XXXXXX";
  ASSERT_NE(mkdtemp(log_dir.data()), nullptr);

  absl::Cleanup cleanup = [log_dir]() {
    (void)RecursivelyRemoveDirectory(log_dir);
  };

  // Create 35 log files
  const std::chrono::system_clock::time_point base_time =
      std::chrono::system_clock::now();
  std::vector<std::string> expected_logs;
  for (int i = 1; i <= 35; ++i) {
    const std::chrono::system_clock::time_point fake_time =
        base_time + std::chrono::seconds(i);
    const std::string path = GetCvdLogFileName(log_dir, fake_time);
    ASSERT_THAT(WriteNewFile(path, "content"), IsOk());
    if (i >= 6) {
      expected_logs.push_back(path.substr(log_dir.size() + 1));
    }
  }

  // Also create one non-log file to ensure it's not pruned
  const std::string non_log = log_dir + "/not_a_log.txt";
  ASSERT_THAT(WriteNewFile(non_log, "content"), IsOk());

  ASSERT_THAT(PruneLogsDirectory(log_dir, 30), IsOk());

  Result<std::vector<std::string>> remaining = DirectoryContents(log_dir);
  ASSERT_THAT(remaining, IsOk());

  EXPECT_TRUE(FileExists(non_log));
  std::erase(*remaining, "not_a_log.txt");

  EXPECT_EQ(remaining->size(), 30);
  // Verify the remaining ones are the newest (i from 6 to 35)
  std::sort(remaining->begin(), remaining->end());
  std::sort(expected_logs.begin(), expected_logs.end());
  EXPECT_EQ(*remaining, expected_logs);
}

}  // namespace
}  // namespace cuttlefish
