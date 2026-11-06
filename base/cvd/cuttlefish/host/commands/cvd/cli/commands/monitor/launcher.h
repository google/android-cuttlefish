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

struct LauncherLine {
  absl::string_view proc_name;
  char severity;
  absl::string_view date;
  absl::string_view time;
  absl::string_view pid;
  absl::string_view tid;
  absl::string_view file_line;
  absl::string_view message;
};

Result<LauncherLine> ParseLauncherLine(absl::string_view line);
std::string FormatLauncherLine(const LauncherLine& line);
Result<std::string> ColorLauncherLine(absl::string_view line);
Result<bool> FilterLauncherLine(LogSeverity filter, absl::string_view line);

}  // namespace cuttlefish
