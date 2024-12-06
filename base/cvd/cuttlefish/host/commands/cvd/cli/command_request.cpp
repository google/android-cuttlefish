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

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/selector/selector_common_parser.h"
#include "host/commands/cvd/cli/utils.h"

namespace cuttlefish {

CommandRequest::CommandRequest(cvd_common::Args args, cvd_common::Envs env,
                               selector::SelectorOptions selectors)
    : args_(std::move(args)),
      env_(std::move(env)),
      selectors_(std::move(selectors)) {}

const cvd_common::Args& CommandRequest::Args() const { return args_; }

const selector::SelectorOptions& CommandRequest::Selectors() const {
  return selectors_;
}

const cvd_common::Envs& CommandRequest::Env() const { return env_; }

std::string CommandRequest::Subcommand() const {
  return ParseInvocation(*this).command;
}

std::vector<std::string> CommandRequest::SubcommandArguments() const {
  return ParseInvocation(*this).arguments;
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
