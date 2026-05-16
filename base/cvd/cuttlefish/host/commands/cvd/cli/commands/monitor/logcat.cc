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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/logcat.h"

#include <cctype>
#include <cstddef>
#include <regex>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/ansi_codes.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/verbosity.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<LogcatLine> ParseLogcatLine(std::string_view line) {
  // Matches structured logcat output format:
  // 05-15 15:28:15.123  1000  1000 I TagName: message
  // Group 1: Timestamp `05-15 15:28:15.123 `
  // Group 2: PIDs, TIDs, Verbosity ` 1000  1000 I`
  // Group 3: (Optional) Tag ` TagName:`
  // Group 4: Message ` message`
  static const std::regex kLogcatRegex(
      R"(^([0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3} )( +[0-9]+ +[0-9]+ +[A-Z])( +[^:]+:)?(.*)$)");
  std::cmatch match;
  CF_EXPECT(std::regex_match(line.begin(), line.end(), match, kLogcatRegex),
            "Failed to parse logcat line");

  std::string_view ids = std::string_view(match[2].first, match[2].length());
  return LogcatLine{
      .timestamp = std::string_view(match[1].first, match[1].length()),
      .verbosity = ids.back(),
      .ids = ids,
      .tag = std::string_view(match[3].first, match[3].length()),
      .message = std::string_view(match[4].first, match[4].length()),
  };
}

std::string FormatLogcatLine(const LogcatLine& line, bool colorize,
                             size_t width) {
  const size_t prefix_len =
      line.timestamp.size() + line.ids.size() + line.tag.size();
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
    message = "";
  }

  if (!colorize) {
    std::string plain =
        absl::StrCat(line.timestamp, line.ids, line.tag, message, padding);
    if (plain.size() > width) {
      plain.resize(width);
    }
    return plain;
  }
  const std::string_view verb_color = GetColorForVerbosity(line.verbosity);
  if (line.tag.empty()) {
    return absl::StrCat(kAnsiGreen, line.timestamp, verb_color, line.ids,
                        kAnsiReset, message, padding);
  }
  return absl::StrCat(kAnsiGreen, line.timestamp, verb_color, line.ids,
                      kAnsiYellow, line.tag, kAnsiReset, message, padding);
}

}  // namespace cuttlefish
