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

#include "host/commands/cvd/server_command/help.h"

#include <fcntl.h>

#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/strings.h>

#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "host/commands/cvd/request_context.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kHelpIntroText[] = R"(Cuttlefish Virtual Device (CVD) CLI.

usage: cvd <selector/driver options> <command> <args>

Selector Options:
  -group_name <name>     Specify the name of the instance group created
                         or selected.
  -instance_name <name>  Selects the device of the given name to perform the
                         commands for.
  -instance_name <names> Takes the names of the devices to create within an
                         instance group. The 'names' is comma-separated.

Driver Options:
  -help                  Print this message
  -verbosity=<LEVEL>     Adjust Cvd verbosity level. LEVEL is Android log
                         severity. (Required: cvd >= v1.3)

Commands (cvd help <command> for more information):)";

constexpr char kSummaryHelpText[] =
    "Used to display help information for other commands";

constexpr char kDetailedHelpText[] =
    R"(cvd help - used to display help text for cvd and its commands

Example usage:
  cvd help - displays summary help for available commands

  cvd help <command> - displays more detailed help for the specific command
)";

constexpr char kIgnorableHandlerCommand[] = "experimental";

}  // namespace

class CvdHelpHandler : public CvdServerHandler {
 public:
  CvdHelpHandler(
      const std::vector<std::unique_ptr<CvdServerHandler>>& request_handlers)
      : request_handlers_(request_handlers) {}

  Result<bool> CanHandle(const CommandRequest& request) const override {
    auto invocation = ParseInvocation(request);
    return (invocation.command == "help");
  }

  Result<cvd::Response> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    auto args = ParseInvocation(request).arguments;
    if (args.empty()) {
      std::cout << CF_EXPECT(TopLevelHelp());
    } else {
      std::cout << CF_EXPECT(SubCommandHelp(args));
    }

    cvd::Response response;
    response.mutable_command_response();  // Sets oneof member
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  cvd_common::Args CmdList() const override { return {"help"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  CommandRequest GetLookupRequest(const std::string& arg) {
    auto result = CommandRequestBuilder().AddArguments({"cvd", arg}).Build();
    CHECK(result.ok()) << "Failed to build cvd command request"
                       << result.error().FormatForEnv();
    return result.value();
  }

  Result<std::string> TopLevelHelp() {
    std::stringstream help_message;
    help_message << kHelpIntroText << std::endl;
    for (const auto& handler : request_handlers_) {
      std::string command_list = android::base::Join(handler->CmdList(), ", ");
      // exclude commands without any command list values as not intended for
      // use by users or sub-subcommands
      if (!command_list.empty() && command_list != kIgnorableHandlerCommand) {
        help_message << "\t" << command_list << " - ";
        help_message << CF_EXPECT(handler->SummaryHelp()) << std::endl
                     << std::endl;
      }
    }
    return help_message.str();
  }

  Result<std::string> SubCommandHelp(std::vector<std::string>& args) {
    CF_EXPECT(
        !args.empty(),
        "Cannot process subcommand help without valid subcommand argument");
    auto lookup_request = GetLookupRequest(args.front());
    auto handler = CF_EXPECT(RequestHandler(lookup_request, request_handlers_));

    std::stringstream help_message;
    help_message << CF_EXPECT(handler->DetailedHelp(args)) << std::endl;
    return help_message.str();
  }

  const std::vector<std::unique_ptr<CvdServerHandler>>& request_handlers_;
};

std::unique_ptr<CvdServerHandler> NewCvdHelpHandler(
    const std::vector<std::unique_ptr<CvdServerHandler>>& request_handlers) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdHelpHandler(request_handlers));
}

}  // namespace cuttlefish
