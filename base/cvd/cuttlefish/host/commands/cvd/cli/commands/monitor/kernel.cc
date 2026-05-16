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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/kernel.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/ansi_codes.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

size_t FindKernelColonOutsideParens(const std::string_view s,
                                    const size_t start_pos) {
  int paren_depth = 0;
  for (size_t i = start_pos; i < s.size(); ++i) {
    if (s[i] == '(') {
      paren_depth++;
    } else if (s[i] == ')') {
      if (paren_depth > 0) {
        paren_depth--;
      }
    } else if (s[i] == ':' && paren_depth == 0) {
      return i;
    }
  }
  return std::string_view::npos;
}

}  // namespace

Result<KernelLine> ParseKernelLine(std::string_view line) {
  KernelLine result;

  CF_EXPECT(!line.empty(), "Line empty");
  CF_EXPECT(line[0] == '[', "Not a bracket");
  const size_t bracket = line.find(']');
  CF_EXPECT(bracket != std::string_view::npos, "No closing bracket");
  result.timestamp = line.substr(0, bracket + 1);

  const size_t colon = FindKernelColonOutsideParens(line, bracket + 1);
  if (colon != std::string_view::npos) {
    result.prefix = line.substr(bracket + 1, colon - bracket);
    result.message = line.substr(colon + 1);
  } else {
    result.prefix = "";
    result.message = line.substr(bracket + 1);
  }

  return result;
}

std::string FormatKernelLine(const KernelLine& line, bool colorize,
                             size_t width) {
  const size_t prefix_len = line.timestamp.size() + line.prefix.size();
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
        absl::StrCat(line.timestamp, line.prefix, message, padding);
    if (plain.size() > width) {
      plain.resize(width);
    }
    return plain;
  }
  if (line.prefix.empty()) {
    return absl::StrCat(kAnsiGreen, line.timestamp, kAnsiReset, message,
                        padding);
  }
  return absl::StrCat(kAnsiGreen, line.timestamp, kAnsiYellow, line.prefix,
                      kAnsiReset, message, padding);
}

}  // namespace cuttlefish
