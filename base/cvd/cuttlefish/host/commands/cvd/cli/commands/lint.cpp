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

#include "host/commands/cvd/cli/commands/lint.h"

#include <memory>
#include <string>
#include <vector>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/commands/server_handler.h"
#include "host/commands/cvd/cli/parser/load_configs_parser.h"
#include "host/commands/cvd/cli/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(error checks the input virtual device json config file)";

constexpr char kDetailedHelpText[] = R"(

Error check of the virtual device json config file.

Usage: cvd lint /path/to/input.json
)";

}  // namespace

class LintCommandHandler : public CvdServerHandler {
 public:
  LintCommandHandler() {}

  Result<bool> CanHandle(const CommandRequest& request) const override {
    return request.Subcommand() == kLintSubCmd;
  }

  Result<cvd::Response> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    std::vector<std::string> args = request.SubcommandArguments();
    auto working_directory = CurrentDirectory();
    const auto config_path = CF_EXPECT(ValidateConfig(args, working_directory));

    std::cout << "Lint of flags and config \"" << config_path
                  << "\" succeeded\n";

    cvd::Response response;
    response.mutable_command_response();
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  cvd_common::Args CmdList() const override { return {kLintSubCmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  Result<std::string> ValidateConfig(std::vector<std::string>& args,
                                     const std::string& working_directory) {
    const LoadFlags flags = CF_EXPECT(GetFlags(args, working_directory));
    CF_EXPECT(GetCvdFlags(flags));
    return flags.config_path;
  }

  static constexpr char kLintSubCmd[] = "lint";
};

std::unique_ptr<CvdServerHandler> NewLintCommand() {
  return std::unique_ptr<CvdServerHandler>(new LintCommandHandler());
}

}  // namespace cuttlefish
