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

#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/flag_parser/flag.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/file.h>

#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

/*
 * From external/gflags/src, commit:
 *  061f68cd158fa658ec0b9b2b989ed55764870047
 *
 */
constexpr std::array help_bool_opts{
    "help", "helpfull", "helpshort", "helppackage", "helpxml", "version", "h"};
constexpr std::array help_str_opts{
    "helpon",
    "helpmatch",
};

}  // namespace
CommandRequest::CommandRequest(cvd_common::Args args, cvd_common::Envs env,
                               selector::SelectorOptions selectors)
    : args_(std::move(args)),
      env_(std::move(env)),
      selectors_(std::move(selectors)) {
  subcommand_arguments_ = args_;
  if (subcommand_arguments_.empty()) {
    return;
  }
  subcommand_arguments_[0] = android::base::Basename(subcommand_arguments_[0]);
  if (subcommand_arguments_[0] == "cvd" && subcommand_arguments_.size() > 1) {
    subcommand_ = subcommand_arguments_[1];
    subcommand_arguments_.erase(subcommand_arguments_.begin());
    subcommand_arguments_.erase(subcommand_arguments_.begin());
  } else {
    subcommand_ = subcommand_arguments_[0];
    subcommand_arguments_.erase(subcommand_arguments_.begin());
  }

  //  transform `cvd -h` or `cvd --help` requests into `cvd help`
  Result<bool> is_top_level_help_flag = HasHelpFlag({subcommand_});
  if (is_top_level_help_flag.ok() && *is_top_level_help_flag) {
    subcommand_ = "help";
  }
}

CommandRequestBuilder& CommandRequestBuilder::AddArguments(
    std::initializer_list<std::string_view> args) & {
  return AddArguments(std::vector<std::string_view>(args));
}

CommandRequestBuilder CommandRequestBuilder::AddArguments(
    std::initializer_list<std::string_view> args) && {
  return AddArguments(std::vector<std::string_view>(args));
}

CommandRequestBuilder& CommandRequestBuilder::SetEnv(cvd_common::Envs env) & {
  env_ = std::move(env);
  return *this;
}

CommandRequestBuilder CommandRequestBuilder::SetEnv(cvd_common::Envs env) && {
  env_ = std::move(env);
  return *this;
}

CommandRequestBuilder& CommandRequestBuilder::AddEnvVar(std::string key,
                                                        std::string val) & {
  env_[key] = val;
  return *this;
}

CommandRequestBuilder CommandRequestBuilder::AddEnvVar(std::string key,
                                                       std::string val) && {
  env_[key] = val;
  return *this;
}

Result<CommandRequest> CommandRequestBuilder::Build() && {
  return CommandRequest(std::move(args_), std::move(env_),
                        std::move(selector_options_));
}

Result<bool> HasHelpFlag(const std::vector<std::string>& args) {
  std::vector<std::string> copied_args(args);
  std::vector<Flag> flags;
  flags.reserve(help_bool_opts.size() + help_str_opts.size());
  bool bool_value_placeholder = false;
  std::string str_value_placeholder;
  for (const auto bool_opt : help_bool_opts) {
    flags.emplace_back(GflagsCompatFlag(bool_opt, bool_value_placeholder));
  }
  for (const auto str_opt : help_str_opts) {
    flags.emplace_back(GflagsCompatFlag(str_opt, str_value_placeholder));
  }
  CF_EXPECT(ConsumeFlags(flags, copied_args));
  // if there was any match, some in copied_args were consumed.
  return (args.size() != copied_args.size());
}

}  // namespace cuttlefish
