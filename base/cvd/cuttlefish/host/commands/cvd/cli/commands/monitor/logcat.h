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

#include <string>

#include "absl/strings/string_view.h"

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/severity.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

struct LogcatLine {
  absl::string_view date;
  absl::string_view time;
  absl::string_view uid;
  absl::string_view pid;
  char severity;
  absl::string_view tag;
  absl::string_view message;
};

Result<LogcatLine> ParseLogcatLine(absl::string_view line);
std::string FormatLogcatLine(const LogcatLine& line);
Result<std::string> ColorLogcatLine(absl::string_view line);
Result<bool> FilterLogcatLine(LogSeverity filter, absl::string_view line);

}  // namespace cuttlefish
