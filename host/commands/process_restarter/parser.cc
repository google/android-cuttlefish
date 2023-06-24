/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/process_restarter/parser.h"

#include <iostream>
#include <string>
#include <vector>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

static constexpr char kIgnoreSigtstpHelp[] =
    "Ignore the sigtstp. This is useful when the managed processes are crosvm"
    "Crosvm has its own logic to be suspended.";
static constexpr char kWhenDumpedHelp[] = "restart when the process crashed";
static constexpr char kWhenKilledHelp[] = "restart when the process was killed";
static constexpr char kWhenExitedWithFailureHelp[] =
    "restart when the process exited with a code !=0";
static constexpr char kWhenExitedWithCodeHelp[] =
    "restart when the process exited with a specific code";
static constexpr char kHelp[] = R"(
    This program launches and automatically restarts the input command
    following the selected restart conditions.
    Example usage:

      ./process_restarter -when_dumped -- my_program --arg1 --arg2)";

/*
 * TODO(288166029): if the flag is not given, do not restart
 * with the exit code of -1 or 255.
 */
Parser::Parser() : when_exited_with_code_(-1) {}

Result<Parser> Parser::ConsumeAndParse(std::vector<std::string>& args) {
  Parser parser;
  std::vector<Flag> flags;
  flags.push_back(parser.IgnoreSigtstpFlag());
  flags.push_back(parser.WhenDumpedFlag());
  flags.push_back(parser.WhenKilledFlag());
  flags.push_back(parser.WhenExitedWithFailureFlag());
  flags.push_back(parser.WhenExitedWithCodeFlag());
  flags.push_back(HelpFlag(flags, kHelp));
  bool matched_help_xml = false;
  flags.push_back(HelpXmlFlag(flags, std::cout, matched_help_xml, ""));
  flags.push_back(UnexpectedArgumentGuard());
  constexpr const bool recognize_end_of_option_mark = true;
  CF_EXPECT(ParseFlags(flags, args, recognize_end_of_option_mark));
  return parser;
}

bool Parser::IgnoreSigtstp() const { return ignore_sigtstp_; }
bool Parser::WhenDumped() const { return when_dumped_; }
bool Parser::WhenKilled() const { return when_killed_; }
bool Parser::WhenExitedWithFailure() const { return when_exited_with_failure_; }
std::int32_t Parser::WhenExitedWithCode() const {
  return when_exited_with_code_;
}

Flag Parser::IgnoreSigtstpFlag() {
  return GflagsCompatFlag("ignore_sigtstp", ignore_sigtstp_)
      .Help(kIgnoreSigtstpHelp);
}

Flag Parser::WhenDumpedFlag() {
  return GflagsCompatFlag("when_dumped", when_dumped_).Help(kWhenDumpedHelp);
}

Flag Parser::WhenKilledFlag() {
  return GflagsCompatFlag("when_killed", when_killed_).Help(kWhenKilledHelp);
}

Flag Parser::WhenExitedWithFailureFlag() {
  return GflagsCompatFlag("when_exited_with_failure", when_exited_with_failure_)
      .Help(kWhenExitedWithFailureHelp);
}

Flag Parser::WhenExitedWithCodeFlag() {
  return GflagsCompatFlag("when_exited_with_code", when_exited_with_code_)
      .Help(kWhenExitedWithCodeHelp);
}

}  // namespace cuttlefish
