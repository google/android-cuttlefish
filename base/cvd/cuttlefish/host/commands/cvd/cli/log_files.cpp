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
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kLogFilePrefix = "cvd_";
constexpr std::string_view kLogFileSuffix = ".log";

bool IsNotLogFile(std::string_view name) {
  const bool prefix_match = absl::StartsWith(name, kLogFilePrefix);
  const bool suffix_match = absl::EndsWith(name, kLogFileSuffix);
  return !(prefix_match && suffix_match);
}

}  // namespace

std::string CvdUserLogDir() { return PerUserDir() + "/logs"; }

std::string GetCvdLogFileName(const std::string& log_dir,
                              const std::chrono::system_clock::time_point now) {
  const std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
  const std::chrono::milliseconds now_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()) %
      1000;

  std::tm tm_now;
  localtime_r(&now_time_t, &tm_now);

  std::stringstream ss;
  ss << log_dir << "/" << kLogFilePrefix
     << std::put_time(&tm_now, "%Y%m%d_%H%M%S") << "." << std::setfill('0')
     << std::setw(3) << now_ms.count() << kLogFileSuffix;
  return ss.str();
}

Result<void> PruneLogsDirectory(const std::string& log_dir, size_t retain) {
  if (!DirectoryExists(log_dir)) {
    return {};
  }
  std::vector<std::string> log_files =
      CF_EXPECTF(DirectoryContents(log_dir),
                 "Failed to list log directory: '{}'", log_dir);
  size_t non_log_files = std::erase_if(log_files, IsNotLogFile);
  if (non_log_files > 0) {
    VLOG(0) << non_log_files << " non-log files found.";
  }

  std::sort(log_files.begin(), log_files.end());

  for (size_t i = 0; i + retain < log_files.size(); ++i) {
    const std::string file_to_remove = log_dir + "/" + log_files[i];
    CF_EXPECTF(RemoveFile(file_to_remove),
               "Failed to remove old log file: '{}'", file_to_remove);
  }
  return {};
}

}  // namespace cuttlefish
