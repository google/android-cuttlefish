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

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"

namespace cuttlefish {

std::vector<std::string> GetLogFiles() {
  std::string log_dir = CvdDir() + "/logs";
  if (!EnsureDirectoryExists(log_dir, 0777).ok()) {
    std::cerr << "Failed to create log directory: " << log_dir
              << ". Logging to file disabled." << std::endl;
    return {};
  }

  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
  std::chrono::milliseconds now_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()) %
      1000;

  std::tm tm_now;
  localtime_r(&now_time_t, &tm_now);

  std::stringstream ss;
  ss << std::put_time(&tm_now, "%Y%m%d_%H%M%S") << "." << std::setfill('0')
     << std::setw(3) << now_ms.count();
  return {absl::StrCat(log_dir, "/cvd_", ss.str(), ".log")};
}

}  // namespace cuttlefish
