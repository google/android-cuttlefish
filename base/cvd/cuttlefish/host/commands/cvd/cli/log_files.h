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

#pragma once

#include <chrono>
#include <string>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

inline constexpr size_t kCvdRetainLogFilesNum = 30;

/** A user-specific directory where log files from `cvd` are stored. */
std::string CvdUserLogDir();

/**
 * Returns the path to a new timestamped log file for the
 * cvd command. The log file is created in PerUserDir() + "/logs/" with a name
 * like cvd_YYYYMMDD_HHMMSS.ms.log. If the logs directory cannot be created, the
 * function returns std::nullopt.
 */
std::string GetCvdLogFileName(const std::string& log_dir = CvdUserLogDir(),
                              std::chrono::system_clock::time_point now =
                                  std::chrono::system_clock::now());

/** Prunes the logs directory keeping only the most recent log files. */
Result<void> PruneLogsDirectory(const std::string& log_dir = CvdUserLogDir(),
                                size_t retain = kCvdRetainLogFilesNum);

}  // namespace cuttlefish
