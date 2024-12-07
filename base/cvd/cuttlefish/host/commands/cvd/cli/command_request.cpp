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

#include "host/commands/cvd/cli/command_request.h"

#include <string>
#include <vector>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/selector/selector_common_parser.h"

namespace cuttlefish {

CommandRequest::CommandRequest(cvd_common::Args args, cvd_common::Envs env,
                               selector::SelectorOptions selectors)
    : args_(std::move(args)),
      env_(std::move(env)),
      selectors_(std::move(selectors)) {
  subcommand_arguments_ = args_;
  if (subcommand_arguments_.empty()) {
    return;
  }
  subcommand_arguments_[0] = cpp_basename(subcommand_arguments_[0]);
  if (subcommand_arguments_[0] == "cvd" && subcommand_arguments_.size() > 1) {
    subcommand_ = subcommand_arguments_[1];
    subcommand_arguments_.erase(subcommand_arguments_.begin());
    subcommand_arguments_.erase(subcommand_arguments_.begin());
  } else {
    subcommand_ = subcommand_arguments_[0];
    subcommand_arguments_.erase(subcommand_arguments_.begin());
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

CommandRequestBuilder& CommandRequestBuilder::AddSelectorArguments(
    std::initializer_list<std::string_view> args) & {
  return AddSelectorArguments(std::vector<std::string_view>(args));
}

CommandRequestBuilder CommandRequestBuilder::AddSelectorArguments(
    std::initializer_list<std::string_view> args) && {
  return AddSelectorArguments(std::vector<std::string_view>(args));
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
  return CommandRequest(
      std::move(args_), std::move(env_),
      CF_EXPECT(selector::ParseCommonSelectorArguments(selector_args_),
                "Failed to parse selector arguments"));
}

}  // namespace cuttlefish
