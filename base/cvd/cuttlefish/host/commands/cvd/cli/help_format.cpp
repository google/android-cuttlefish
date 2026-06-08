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

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/flag_parser/flag.h"

namespace cuttlefish {
namespace {

std::vector<std::string> WrapAroundLine(std::string_view str,
                                        size_t max_line_length) {
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

}  // namespace

HelpParagraph HelpParagraph::Raw(std::string text) {
  return HelpParagraph(std::move(text), Style::Raw);
}

HelpParagraph::HelpParagraph(std::string text)
    : text_(std::move(text)), style_(Style::Wrapped) {}
HelpParagraph::HelpParagraph(std::string text, Style style)
    : text_(std::move(text)), style_(style) {}

std::string HelpParagraph::Formatted(size_t max_line_width) const {
  switch(style_) {
    case Style::Wrapped:
      return absl::StrJoin(WrapAroundLine(text_, max_line_width), "\n");
    case Style::Raw:
      return text_;
  }
}

std::string FormatHelpText(const std::vector<HelpParagraph>& text,
                           size_t max_line_width) {
  std::stringstream ss;
  for (const HelpParagraph& paragraph : text) {
    // Empty line after paragraphs
    ss << paragraph.Formatted(max_line_width) << "\n\n";
  }
  return ss.str();
}

std::string FormatFlagsHelp(const std::vector<Flag>& flags,
                            size_t max_line_width) {
  std::stringstream ss;
  for (const Flag& flag : flags) {
    ss << " " << flag.Synopsis() << "\n";
    for (std::string_view wrapped_line :
         WrapAroundLine(flag.Description(), max_line_width - 4)) {
      ss << "    " << wrapped_line << "\n";
    }
    ss << "    Current value: \"" << flag.CurrentValue() << "\"\n";
    ss << "\n";
  }
  return ss.str();
}

}  // namespace cuttlefish
