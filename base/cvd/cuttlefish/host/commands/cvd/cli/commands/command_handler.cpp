/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"

#include <stddef.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr size_t kMaxLineLength = 80;

std::vector<std::string> WrapAroundLine(
    std::string_view str, size_t max_line_length = kMaxLineLength) {
  std::vector<std::string> ret;
  std::vector<std::string_view> words = absl::StrSplit(str, ' ');
  size_t total_word_sizes = 0;
  std::vector<std::string_view> line;
  for (std::string_view word : words) {
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

bool CvdCommandHandler::RequiresDeviceExists() const { return false; }

std::vector<std::string> CvdCommandHandler::Description() const { return {}; }

Result<std::vector<Flag>> CvdCommandHandler::Flags(const CommandRequest&) {
  return {};
}

Result<std::string> CvdCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  std::stringstream ss;
  std::vector<std::string> cmd_list = CmdList();
  CF_EXPECT(!cmd_list.empty(), "Command aliases list is empty");

  ss << "cvd " << cmd_list[0] << " - " << SummaryHelp() << "\n";
  ss << "\n";
  for (const std::string& paragraph : Description()) {
    for (std::string_view line : WrapAroundLine(paragraph)) {
      ss << line << "\n";
    }
    // Empty line after paragraphs
    ss << "\n";
  }

  std::vector<Flag> flags = CF_EXPECT(Flags(request));
  // Consume flags to ensure "current value" is accurate
  cvd_common::Args args = request.SubcommandArguments();

  CF_EXPECT(ConsumeFlags(flags, args));
  selector::SelectorOptions selector_options = request.Selectors();
  if (RequiresDeviceExists()) {
    // Add the common selector flags if the command supports them. This doesn't
    // need to hapen before consuming as the selector flags were consumed
    // already. Using the selectors from the request ensures the flags's
    // "current value" is correct.
    std::vector<Flag> selector_flags =
        selector::BuildCommonSelectorFlags(selector_options);
    flags.insert(flags.end(), selector_flags.begin(), selector_flags.end());
  }

  // Make sure the flags are in alphabetical order
  std::sort(flags.begin(), flags.end());

  for (const Flag& flag : flags) {
    std::stringstream ss2;
    ss2 << flag;
    std::string flag_help = ss2.str();
    std::vector<std::string_view> flag_help_lines =
        absl::StrSplit(flag_help, '\n');
    bool first_line = true;
    for (std::string_view& line : flag_help_lines) {
      if (line.empty()) {
        continue;
      }
      if (first_line) {
        // The first line contains the --flag=value examples, don't indent or
        // wrap those.
        ss << line << "\n";
        first_line = false;
      } else {
        for (std::string_view l : WrapAroundLine(line, kMaxLineLength - 4)) {
          ss << "    " << l << "\n";
        }
      }
    }
    ss << "\n";
  }

  return ss.str();
}

}  // namespace cuttlefish
