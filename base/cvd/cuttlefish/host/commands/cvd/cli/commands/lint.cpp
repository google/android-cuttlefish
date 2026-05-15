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

#include "cuttlefish/host/commands/cvd/cli/commands/lint.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_configs_parser.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kLintSubCmd[] = "lint";

constexpr char kSummaryHelpText[] =
    R"(error checks the input virtual device json config file)";

constexpr char kDetailedHelpText[] = R"(

Error check of the virtual device json config file.

Usage: cvd lint /path/to/input.json
)";

}  // namespace

Result<void> LintCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  auto working_directory = CurrentDirectory();
  const auto config_path = CF_EXPECT(ValidateConfig(args, working_directory));

  std::cout << "Lint of flags and config \"" << config_path << "\" succeeded\n";

  return {};
}

cvd_common::Args LintCommandHandler::CmdList() const { return {kLintSubCmd}; }

std::string LintCommandHandler::SummaryHelp() const { return kSummaryHelpText; }

Result<std::string> LintCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  return kDetailedHelpText;
}

Result<std::string> LintCommandHandler::ValidateConfig(
    std::vector<std::string>& args, const std::string& working_directory) {
  const LoadFlags flags = CF_EXPECT(GetFlags(args, working_directory));
  CF_EXPECT(GetEnvironmentSpecification(flags));
  return flags.config_path;
}

std::unique_ptr<CvdCommandHandler> NewLintCommand() {
  return std::unique_ptr<CvdCommandHandler>(new LintCommandHandler());
}

}  // namespace cuttlefish
