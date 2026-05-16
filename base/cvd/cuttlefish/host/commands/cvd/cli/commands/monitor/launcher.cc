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
#include <regex>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/ansi_codes.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/verbosity.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<LauncherLine> ParseLauncherLine(std::string_view line) {
  // clang-format off
  // Matches structured launcher log output format:
  // kernel_log_monitor(4089384)  I 05-15 16:39:26 4089384 4089384 kernel_log_server.cc:153] VIRTUAL_DEVICE_BOOT_COMPLETED
  // Group 1: Process name `kernel_log_monitor`
  // (Skipped): PID in parens `(4089384)  `
  // Group 2: Verbosity `I`
  // Group 3: Timestamp `05-15 16:39:26`
  // Group 4: PIDs/TIDs ` 4089384 4089384`
  // (Skipped): File & line number ` kernel_log_server.cc:153]`
  // Group 5: Message ` VIRTUAL_DEVICE_BOOT_COMPLETED`
  // clang-format on
  static const std::regex kLauncherRegex(
      R"(^([^(]+)\([^)]+\) +([A-Z]) +([0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2})( +[0-9]+ +[0-9]+) +[^ ]+\](.*)$)");
  std::cmatch match;
  CF_EXPECT(std::regex_match(line.begin(), line.end(), match, kLauncherRegex),
            "Failed to parse launcher line");

  return LauncherLine{
      .timestamp = std::string_view(match[3].first, match[3].length()),
      .ids = std::string_view(match[4].first, match[4].length()),
      .verbosity = *match[2].first,
      .proc_name = std::string_view(match[1].first, match[1].length()),
      .message = std::string_view(match[5].first, match[5].length()),
  };
}

std::string FormatLauncherLine(const LauncherLine& line, bool colorize,
                               size_t width) {
  std::string plain_prefix = absl::StrCat(line.timestamp, line.ids, " ",
                                          std::string_view(&line.verbosity, 1),
                                          " ", line.proc_name, ":");
  const size_t prefix_len = plain_prefix.size();
  std::string_view message = line.message;
  std::string padding;
  if (prefix_len < width) {
    const size_t max_msg_len = width - prefix_len;
    if (message.size() > max_msg_len) {
      message = message.substr(0, max_msg_len);
    } else {
      padding = std::string(max_msg_len - message.size(), ' ');
    }
  } else {
    plain_prefix.resize(width);
    message = "";
  }

  if (!colorize) {
    std::string plain = absl::StrCat(plain_prefix, message, padding);
    if (plain.size() > width) {
      plain.resize(width);
    }
    return plain;
  }
  if (prefix_len >= width) {
    return absl::StrCat(kAnsiGreen, plain_prefix, kAnsiReset);
  }
  const std::string_view verb_color = GetColorForVerbosity(line.verbosity);
  return absl::StrCat(kAnsiGreen, line.timestamp, verb_color, line.ids, " ",
                      std::string_view(&line.verbosity, 1), " ", kAnsiYellow,
                      line.proc_name, ":", kAnsiReset, message, padding);
}

}  // namespace cuttlefish
