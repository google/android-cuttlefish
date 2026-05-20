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

#include "cuttlefish/host/commands/cvd/cli/help_format.h"

#include <sstream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/flag_parser.h"

namespace cuttlefish {
namespace {

constexpr char kRawTextMark[] = "_RAW_TEXT:";

std::vector<std::string> WrapAroundLine(std::string_view str,
                                        size_t max_line_length) {
  if (absl::ConsumePrefix(&str, kRawTextMark)) {
    return {std::string(str)};
  }
  std::vector<std::string> ret;
  size_t total_word_sizes = 0;
  std::vector<std::string_view> line;
  for (std::string_view word : absl::StrSplit(str, absl::ByAnyChar(" \t\r\n"))) {
    // line.size() accounts for the spaces added when joining the words.
    if (total_word_sizes + word.size() + line.size() >= max_line_length) {
      // If the line is empty at this point it means the current word is too
      // long, it will be added to the line anyways and printed on its own in
      // the next iteration.
      if (!line.empty()) {
        ret.push_back(absl::StrJoin(line, " "));
      }
      total_word_sizes = 0;
      line.clear();
    }
    total_word_sizes += word.size();
    line.push_back(word);
  }
  if (!line.empty()) {
    // Print the last line
    ret.push_back(absl::StrJoin(line, " "));
  }
  return ret;
}

std::vector<std::string> GetFlagHelpMessage(const Flag& flag) {
  std::stringstream ss;
  ss << flag;
  std::vector<std::string> flag_help_lines =
      absl::StrSplit(ss.str(), '\n', absl::SkipWhitespace());
  return flag_help_lines;
}

}  // namespace

std::string FormatHelpText(const std::vector<std::string>& text,
                           size_t max_line_width) {
  std::stringstream ss;
  for (const std::string& paragraph : text) {
    for (std::string_view line : WrapAroundLine(paragraph, max_line_width)) {
      ss << line << "\n";
    }
    // Empty line after paragraphs
    ss << "\n";
  }
  return ss.str();
}

std::string FormatFlagsHelp(const std::vector<Flag>& flags,
                            size_t max_line_width) {
  std::stringstream ss;
  for (const Flag& flag : flags) {
    std::vector<std::string> help_lines = GetFlagHelpMessage(flag);
    CHECK(!help_lines.empty())
        << "Flag produced empty help message: " << flag.Name();
    // The first line contains the --flag=value examples, don't indent or wrap
    // those.
    ss << help_lines.front() << "\n";
    for (size_t i = 1; i < help_lines.size(); ++i) {
      for (std::string_view l :
           WrapAroundLine(help_lines[i], max_line_width - 4)) {
        ss << "    " << l << "\n";
      }
    }
    ss << "\n";
  }
  return ss.str();
}

std::string MarkAsRawText(std::string_view str) {
  return absl::StrCat(kRawTextMark, str);
}

}  // namespace cuttlefish
