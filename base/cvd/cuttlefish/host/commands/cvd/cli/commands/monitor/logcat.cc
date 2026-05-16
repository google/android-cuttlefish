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

Result<LogcatLine> ParseLogcatLine(std::string_view line) {
  // clang-format off
  // Sample line for reference:
  // 05-15 15:28:15.123  1000  1000 I TagName: message
  // clang-format on

  // Split on any whitespace sequence, ignoring empty results to handle tabs and
  // multiple spaces.
  // Note: We avoid absl::MaxSplits here because it counts delimiter matches
  // *before* absl::SkipWhitespace filters them, which breaks index alignment on
  // double-spaces.
  std::vector<std::string_view> fields =
      absl::StrSplit(line, absl::ByAnyChar(" \t"), absl::SkipWhitespace());

  CF_EXPECT(fields.size() > 5, "Failed to parse Logcat line");

  CF_EXPECT(fields[4].size() == 1, "Invalid verbosity indicator");
  char verbosity = fields[4][0];

  std::string_view tag;
  std::string_view message;

  // Remainder starts at Field 5
  const char* remainder_start = fields[5].data();
  const size_t remainder_len = line.data() + line.size() - remainder_start;
  std::string_view remainder(remainder_start, remainder_len);

  size_t colon = remainder.find(':');
  if (colon == std::string_view::npos) {
    tag = "";
    message = remainder;
  } else {
    tag = remainder.substr(0, colon + 1);  // "TagName:"
    message = remainder.substr(colon + 1);
    if (!message.empty() && (message[0] == ' ' || message[0] == '\t')) {
      message.remove_prefix(1);
    }
  }

  return LogcatLine{
      .date = fields[0],
      .time = fields[1],
      .uid = fields[2],
      .pid = fields[3],
      .verbosity = verbosity,
      .tag = tag,
      .message = message,
  };
}

std::string FormatLogcatLine(const LogcatLine& line) {
  const std::string_view verb_color = GetColorForVerbosity(line.verbosity);
  std::string result = absl::StrCat(kAnsiGreen, line.date, " ", line.time, " ",
                                    verb_color, line.uid, " ", line.pid, " ",
                                    std::string_view(&line.verbosity, 1), " ");

  if (!line.tag.empty()) {
    absl::StrAppend(&result, kAnsiYellow, line.tag, " ");
  }

  absl::StrAppend(&result, kAnsiReset, line.message);
  return result;
}

}  // namespace cuttlefish
