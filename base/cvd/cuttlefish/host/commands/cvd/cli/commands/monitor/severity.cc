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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/severity.h"

#include <string_view>

#include "cuttlefish/ansi_codes/ansi_codes.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<LogSeverity> CharToLogSeverity(char severity) {
  switch (severity) {
    case 'E':
      return LogSeverity::Error;
    case 'W':
      return LogSeverity::Warning;
    case 'I':
      return LogSeverity::Info;
    case 'D':
      return LogSeverity::Debug;
    case 'V':
      return LogSeverity::Verbose;
    default:
      return CF_ERR("Unknown");
  }
}

std::string_view GetColorForSeverity(LogSeverity severity) {
  switch (severity) {
    case LogSeverity::Error:
      return kAnsiRed;
    case LogSeverity::Warning:
      return kAnsiYellow;
    case LogSeverity::Info:
      return kAnsiGreen;
    case LogSeverity::Debug:
      return kAnsiCyan;
    case LogSeverity::Verbose:
      return kAnsiWhite;
    default:
      return "";
  }
}

bool FilterSeverity(LogSeverity filter_severity, LogSeverity test_severity) {
  return static_cast<int>(test_severity) >= static_cast<int>(filter_severity);
}

}  // namespace cuttlefish
