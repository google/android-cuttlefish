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

#include <stddef.h>

#include <string>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"

namespace cuttlefish {

constexpr size_t kDefaultMaxLineWidth = 80;

class HelpParagraph {
 public:
  static HelpParagraph Raw(std::string text);

  explicit HelpParagraph(std::string);

  std::string Formatted(size_t max_line_width) const;

 private:
  enum class Style {
    Wrapped,
    Raw,
  };

  HelpParagraph(std::string, Style style);

  std::string text_;
  Style style_;
};

// Formats text (typically a command description) to be displayed in the
// terminal. Each string in the input is considered to be a different paragraph
// and should not contain line changes. Each paragraph will be broken into lines
// of at most max_line_width columns without splitting individual words. An
// empty line will be added after each paragraph.
std::string FormatHelpText(const std::vector<HelpParagraph>& text,
                           size_t max_line_width = kDefaultMaxLineWidth);

// Formats the help messages of a list of flags to be displayed in the terminal.
// The return string will contain lines of at most max_line_width characters
// except when necessary to avoid splitting a word.
std::string FormatFlagsHelp(const std::vector<Flag>& flags,
                            size_t max_line_width = kDefaultMaxLineWidth);
}  // namespace cuttlefish
