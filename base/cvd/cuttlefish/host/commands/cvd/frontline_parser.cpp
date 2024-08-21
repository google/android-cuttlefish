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

#include "host/commands/cvd/frontline_parser.h"

#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

Result<cvd_common::Args> ExtractCvdArgs(cvd_common::Args& args) {
  FrontlineParser::ParserParam server_param{
      .server_supported_subcmds = {"*"},
      .all_args = args
  };
  auto frontline_parser = CF_EXPECT(FrontlineParser::Parse(server_param));
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

Result<std::unique_ptr<FrontlineParser>> FrontlineParser::Parse(
    ParserParam param) {
  CF_EXPECT(!param.all_args.empty());
  FrontlineParser* frontline_parser = new FrontlineParser(param);
  CF_EXPECT(frontline_parser != nullptr,
            "Memory allocation for FrontlineParser failed.");
  CF_EXPECT(frontline_parser->Separate());
  return std::unique_ptr<FrontlineParser>(frontline_parser);
}

FrontlineParser::FrontlineParser(const ParserParam& param)
    : server_supported_subcmds_{param.server_supported_subcmds},
      all_args_(param.all_args) {}

Result<void> FrontlineParser::Separate() {
  arguments_separator_ = CF_EXPECT(CallSeparator());
  return {};
}

Result<cvd_common::Args> FrontlineParser::ValidSubcmdsList() {
  cvd_common::Args valid_subcmds(server_supported_subcmds_);
  return valid_subcmds;
}

static Result<std::unordered_set<std::string>> BoolFlagNames(
    const std::vector<CvdFlagProxy>& flags) {
  std::unordered_set<std::string> output;
  for (const auto& flag : flags) {
    if (flag.GetType() == CvdFlagProxy::FlagType::kBool) {
      output.insert(CF_EXPECT(flag.Name()));
    }
  }
  return output;
}

static Result<std::unordered_set<std::string>> ValueFlagNames(
    const std::vector<CvdFlagProxy>& flags) {
  std::unordered_set<std::string> output;
  for (const auto& flag : flags) {
    if (flag.GetType() == CvdFlagProxy::FlagType::kInt32 ||
        flag.GetType() == CvdFlagProxy::FlagType::kString) {
      output.insert(CF_EXPECT(flag.Name()));
    }
  }
  return output;
}

Result<std::unique_ptr<selector::ArgumentsSeparator>>
FrontlineParser::CallSeparator() {
  auto valid_subcmds_vector = CF_EXPECT(ValidSubcmdsList());
  std::unordered_set<std::string> valid_subcmds{valid_subcmds_vector.begin(),
                                                valid_subcmds_vector.end()};
  auto cvd_flags = CF_EXPECT(selector::SelectorFlags::New()).Flags();

  auto known_bool_flags = CF_EXPECT(BoolFlagNames(cvd_flags));
  auto known_value_flags = CF_EXPECT(ValueFlagNames(cvd_flags));

  ArgumentsSeparator::FlagsRegistration flag_registration{
      .known_boolean_flags = known_bool_flags,
      .known_value_flags = known_value_flags,
      .valid_subcommands = valid_subcmds};
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

}  // namespace cuttlefish
