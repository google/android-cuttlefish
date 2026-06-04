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

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/ansi_codes.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/verbosity.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<LauncherLine> ParseLauncherLine(std::string_view line) {
  // clang-format off
  // Sample line for reference:
  // kernel_log_monitor(4089384)  I 05-15 16:39:26 4089384 4089384 kernel_log_server.cc:153] VIRTUAL_DEVICE_BOOT_COMPLETED
  // clang-format on

  // Split on any whitespace sequence, ignoring empty results to handle tabs and
  // multiple spaces.
  // Note: We avoid absl::MaxSplits here because it counts delimiter matches
  // *before* absl::SkipWhitespace filters them, which breaks index alignment on
  // double-spaces.
  std::vector<std::string_view> fields =
      absl::StrSplit(line, absl::ByAnyChar(" \t"), absl::SkipWhitespace());

  CF_EXPECT(fields.size() > 7, "Failed to parse Launcher line");

  // Extract process name from fields[0] (e.g., "kernel_log_monitor(4089384)")
  std::string_view proc_name = fields[0];
  const size_t paren = proc_name.find('(');
  if (paren != std::string_view::npos) {
    proc_name = proc_name.substr(0, paren);
  }

  CF_EXPECT(fields[1].size() == 1, "Invalid verbosity indicator");
  char verbosity = fields[1][0];

  // Capture message from the 8th field (index 7) to the end of the line.
  // This preserves any internal spacing within the unstructured message.
  const char* message_start = fields[7].data();
  const size_t message_len = line.data() + line.size() - message_start;
  std::string_view message(message_start, message_len);

  return LauncherLine{
      .proc_name = proc_name,
      .verbosity = verbosity,
      .date = fields[2],
      .time = fields[3],
      .pid = fields[4],
      .tid = fields[5],
      .file_line = fields[6],
      .message = message,
  };
}

std::string FormatLauncherLine(const LauncherLine& line) {
  const std::string_view verb_color = GetColorForVerbosity(line.verbosity);
  return absl::StrCat(kAnsiGreen, line.date, " ", line.time, " ", verb_color,
                      line.pid, " ", line.tid, " ",
                      std::string_view(&line.verbosity, 1), " ", kAnsiYellow,
                      line.proc_name, ": ", kAnsiReset, line.message);
}

}  // namespace cuttlefish
