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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/log_tee.h"

#include <cstddef>
#include <regex>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/ansi_codes.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/verbosity.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<LogTeeLine> ParseLogTeeLine(std::string_view line) {
  // Matches structured log_tee output format:
  // [2026-05-15T23:43:46.448645816+00:00 INFO  disk] disk size 294518784
  // Group 1: Date prefix `[2026-05-15T...`
  // Group 2: Verbosity `INFO`
  // Group 3: Subsystem `disk]`
  // Group 4: Message ` disk size 294518784`
  static const std::regex kLogTeeRegex(
      R"(^(\[[^ ]+) +([A-Z]+) +([^\]]+\])(.*)$)");
  std::cmatch match;
  CF_EXPECT(std::regex_match(line.begin(), line.end(), match, kLogTeeRegex),
            "Failed to parse log_tee line");

  return LogTeeLine{
      .date = std::string_view(match[1].first, match[1].length()),
      .verbosity = std::string_view(match[2].first, match[2].length()),
      .subsystem = std::string_view(match[3].first, match[3].length()),
      .message = std::string_view(match[4].first, match[4].length()),
  };
}

std::string FormatLogTeeLine(const LogTeeLine& line, bool colorize,
                             size_t width) {
  std::string plain_prefix =
      absl::StrCat(line.date, " ", line.verbosity, " ", line.subsystem);
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
  std::string_view verb_color;
  if (line.verbosity == "ERROR") {
    verb_color = GetColorForVerbosity('E');
  } else if (line.verbosity == "WARN") {
    verb_color = GetColorForVerbosity('W');
  } else if (line.verbosity == "INFO") {
    verb_color = GetColorForVerbosity('I');
  } else if (line.verbosity == "DEBUG") {
    verb_color = GetColorForVerbosity('D');
  }

  return absl::StrCat(kAnsiGreen, line.date, " ", verb_color, line.verbosity,
                      " ", kAnsiYellow, line.subsystem, kAnsiReset, message,
                      padding);
}

}  // namespace cuttlefish
