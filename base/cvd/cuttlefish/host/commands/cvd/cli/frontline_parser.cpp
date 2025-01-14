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

#include "host/commands/cvd/cli/frontline_parser.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "host/commands/cvd/cli/flag.h"
#include "host/commands/cvd/cli/selector/arguments_separator.h"
#include "host/commands/cvd/cli/selector/selector_constants.h"
#include "host/commands/cvd/cli/types.h"

namespace cuttlefish {
namespace {

/* the very first command line parser
 *
 * Being aware of valid subcommands and cvd-specific commands, it will
 * separate the command line arguments into:
 *
 *  1. program path/name
 *  2. cvd-specific arguments
 *     a) selector flags
 *     b) non-selector flags
 *  3. subcommand
 *  4. subcommand arguments
 *
 * This is currently on the client side but will be moved to the server
 * side.
 */
class FrontlineParser {
  using ArgumentsSeparator = selector::ArgumentsSeparator;

 public:
  // This call must guarantee all public methods will be valid
  static Result<std::unique_ptr<FrontlineParser>> Parse(
      const cvd_common::Args& all_args);

  const std::string& ProgPath() const;
  std::optional<std::string> SubCmd() const;
  const cvd_common::Args& SubCmdArgs() const;
  const cvd_common::Args& CvdArgs() const;

 private:
  FrontlineParser(const cvd_common::Args& all_args);

  // internal workers in order
  Result<void> Separate();
  Result<std::unique_ptr<ArgumentsSeparator>> CallSeparator();
  struct FilterOutput {
    bool clean;
    bool help;
    cvd_common::Args selector_args;
  };
  Result<FilterOutput> FilterNonSelectorArgs();

  const cvd_common::Args all_args_;
  const std::vector<std::string> internal_cmds_;
  std::unique_ptr<ArgumentsSeparator> arguments_separator_;
};

Result<std::unique_ptr<FrontlineParser>> FrontlineParser::Parse(
    const cvd_common::Args& all_args) {
  CF_EXPECT(!all_args.empty());
  std::unique_ptr<FrontlineParser> frontline_parser(
      new FrontlineParser(all_args));
  CF_EXPECT(frontline_parser != nullptr,
            "Memory allocation for FrontlineParser failed.");
  CF_EXPECT(frontline_parser->Separate());
  return frontline_parser;
}

FrontlineParser::FrontlineParser(const cvd_common::Args& all_args)
    : all_args_(all_args) {}

Result<void> FrontlineParser::Separate() {
  arguments_separator_ = CF_EXPECT(CallSeparator());
  return {};
}

Result<std::unique_ptr<selector::ArgumentsSeparator>>
FrontlineParser::CallSeparator() {
  ArgumentsSeparator::FlagsRegistration flag_registration{
      .known_boolean_flags = {},
      .known_value_flags = {selector::SelectorFlags::kGroupName,
                            selector::SelectorFlags::kInstanceName,
                            selector::SelectorFlags::kVerbosity},
  };
  auto arguments_separator =
      CF_EXPECT(ArgumentsSeparator::Parse(flag_registration, all_args_));
  CF_EXPECT(arguments_separator != nullptr);
  return arguments_separator;
}

const std::string& FrontlineParser::ProgPath() const {
  return arguments_separator_->ProgPath();
}

std::optional<std::string> FrontlineParser::SubCmd() const {
  return arguments_separator_->SubCmd();
}

const cvd_common::Args& FrontlineParser::SubCmdArgs() const {
  return arguments_separator_->SubCmdArgs();
}

const cvd_common::Args& FrontlineParser::CvdArgs() const {
  return arguments_separator_->CvdArgs();
}

}  // namespace

Result<cvd_common::Args> ExtractCvdArgs(cvd_common::Args& args) {
  auto frontline_parser = CF_EXPECT(FrontlineParser::Parse(args));
  CF_EXPECT(frontline_parser != nullptr);

  const auto prog_path = frontline_parser->ProgPath();
  const auto new_sub_cmd = frontline_parser->SubCmd();
  cvd_common::Args cmd_args{frontline_parser->SubCmdArgs()};

  cvd_common::Args new_exec_args{prog_path};
  if (new_sub_cmd) {
    new_exec_args.push_back(*new_sub_cmd);
  }
  new_exec_args.insert(new_exec_args.end(), cmd_args.begin(), cmd_args.end());
  args = new_exec_args;
  return frontline_parser->CvdArgs();
}

}  // namespace cuttlefish
