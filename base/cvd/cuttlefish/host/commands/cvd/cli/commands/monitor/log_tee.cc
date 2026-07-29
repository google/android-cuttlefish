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

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/ansi_codes/ansi_codes.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/severity.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<LogTeeLine> ParseLogTeeLine(std::string_view line) {
  // Split on any whitespace sequence, ignoring empty results to handle tabs and
  // multiple spaces.
  // Note: We avoid absl::MaxSplits here because it counts delimiter matches
  // *before* absl::SkipWhitespace filters them, which breaks index alignment on
  // double-spaces.
  std::vector<std::string_view> fields =
      absl::StrSplit(line, absl::ByAnyChar(" \t"), absl::SkipWhitespace());
  CF_EXPECT(fields.size() >= 3, "Failed to parse log_tee line");

  std::string_view date = fields[0];
  CF_EXPECT(absl::StartsWith(date, "["), "Date does not start with '['");

  std::string_view severity = fields[1];
  for (const char c : severity) {
    CF_EXPECT(std::isupper(c), "Invalid severity characters");
  }

  std::string_view subsystem = fields[2];
  CF_EXPECT(absl::EndsWith(subsystem, "]"), "Subsystem does not end with ']'");

  const char* message_start = fields[2].data() + fields[2].size();
  const size_t message_len = line.data() + line.size() - message_start;
  std::string_view message(message_start, message_len);

  return LogTeeLine{
      .date = date,
      .severity = severity,
      .subsystem = subsystem,
      .message = message,
  };
}

std::string FormatLogTeeLine(const LogTeeLine& line) {
  const std::string_view severity_color =
      ToSeverity(line.severity).transform(GetColorForSeverity).value_or("");

  return absl::StrCat(kAnsiGreen, line.date, " ", severity_color, line.severity,
                      " ", kAnsiYellow, line.subsystem, kAnsiReset,
                      line.message);
}

Result<std::string> ColorLogTeeLine(std::string_view line) {
  return FormatLogTeeLine(CF_EXPECT(ParseLogTeeLine(line)));
}

Result<bool> FilterLogTeeLine(LogSeverity filter, std::string_view line) {
  const LogTeeLine parsed = CF_EXPECT(ParseLogTeeLine(line));
  LogSeverity line_severity = CF_EXPECT(ToSeverity(parsed.severity));
  return FilterSeverity(filter, line_severity);
}

}  // namespace cuttlefish
