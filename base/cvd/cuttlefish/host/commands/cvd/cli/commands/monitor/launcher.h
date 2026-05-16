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

#include <cstddef>
#include <string>
#include <string_view>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct LauncherLine {
  std::string_view proc_name;
  char verbosity;
  std::string_view date;
  std::string_view time;
  std::string_view pid;
  std::string_view tid;
  std::string_view file_line;
  std::string_view message;
};
Result<LauncherLine> ParseLauncherLine(std::string_view line);
std::string FormatLauncherLine(const LauncherLine& line);

}  // namespace cuttlefish
