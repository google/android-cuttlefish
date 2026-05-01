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

#include "cuttlefish/host/commands/cvd/cli/commands/help.h"

#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_join.h"

#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/request_context.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kHelpIntroText[] = R"(Cuttlefish Virtual Device (CVD) CLI.

usage: cvd <selector/driver options> <command> <args>

Driver Options:
  -help                  Print this message
  -verbosity=<LEVEL>     Adjust Cvd verbosity level. LEVEL is Android log
                         severity. (Required: cvd >= v1.3)

Commands (cvd help <command> for more information):

)";

constexpr char kSelectorOptionsText[] = R"(
Selector Options:
  -group_name <name>     Specify the name of the instance group created
                         or selected.
  -instance_name <name>  Selects the device of the given name to perform the
                         commands for.
  -instance_name <names> Takes the names of the devices to create within an
                         instance group. The 'names' is comma-separated.
)";

constexpr char kSummaryHelpText[] =
    "Used to display help information for other commands";

constexpr char kDetailedHelpText[] =
    R"(cvd help - used to display help text for cvd and its commands

Example usage:
  cvd help - displays summary help for available commands

  cvd help <command> - displays more detailed help for the specific command
)";

}  // namespace

class CvdHelpHandler : public CvdCommandHandler {
 public:
  CvdHelpHandler(
      const std::vector<std::unique_ptr<CvdCommandHandler>>& request_handlers)
      : request_handlers_(request_handlers) {}

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    std::vector<std::string> args = request.SubcommandArguments();
    if (args.empty()) {
      std::cout << CF_EXPECT(TopLevelHelp());
    } else {
      std::cout << CF_EXPECT(SubCommandHelp(args));
    }

    return {};
  }

  cvd_common::Args CmdList() const override { return {"help"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  Result<CommandRequest> GetLookupRequest(const std::string& arg) {
    auto builder = CommandRequestBuilder().AddArguments({"cvd", arg});
    return CF_EXPECT(std::move(builder).Build());
  }

  Result<std::string> TopLevelHelp() {
    std::stringstream help_message;
    help_message << kHelpIntroText;

    for (const auto& handler : request_handlers_) {
      if (!handler->RequiresDeviceExists()) {
        CF_EXPECT(PrintHandler(help_message, *handler));
      }
    }

    help_message << kSelectorOptionsText;
    help_message << "\nDevice-Specific Commands (cvd help <command> for more "
                    "information):\n";
    for (const auto& handler : request_handlers_) {
      if (handler->RequiresDeviceExists()) {
        CF_EXPECT(PrintHandler(help_message, *handler));
      }
    }

    return help_message.str();
  }

  Result<void> PrintHandler(std::stringstream& help_message,
                            const CvdCommandHandler& handler) const {
    help_message << "\t" << absl::StrJoin(handler.CmdList(), ", ") << " - ";
    help_message << CF_EXPECT(handler.SummaryHelp()) << "\n\n";
    return {};
  }

  Result<std::string> SubCommandHelp(std::vector<std::string>& args) {
    CF_EXPECT(
        !args.empty(),
        "Cannot process subcommand help without valid subcommand argument");
    CommandRequest lookup_request = CF_EXPECT(GetLookupRequest(args.front()));
    auto handler = CF_EXPECT(RequestHandler(lookup_request, request_handlers_));

    std::stringstream help_message;
    help_message << CF_EXPECT(handler->DetailedHelp(args)) << std::endl;
    return help_message.str();
  }

  const std::vector<std::unique_ptr<CvdCommandHandler>>& request_handlers_;
};

std::unique_ptr<CvdCommandHandler> NewCvdHelpHandler(
    const std::vector<std::unique_ptr<CvdCommandHandler>>& server_handlers) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdHelpHandler(server_handlers));
}

}  // namespace cuttlefish
