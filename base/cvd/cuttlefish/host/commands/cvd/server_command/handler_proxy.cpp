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

#include "host/commands/cvd/server_command/handler_proxy.h"

#include <vector>

#include <android-base/strings.h>

#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/frontline_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CvdServerHandlerProxy : public CvdServerHandler {
 public:
  CvdServerHandlerProxy(CommandSequenceExecutor& executor)
      : executor_(executor) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return (invocation.command == "process");
  }

  // the input format is:
  //   cmd_args:      cvd cmdline-parser
  //   selector_args: [command args to parse]
  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));

    const auto& selector_opts =
        request.Message().command_request().selector_opts();
    auto all_args = cvd_common::ConvertToArgs(selector_opts.args());
    CF_EXPECT_GE(all_args.size(), 1ul);
    if (all_args.size() == 1ul) {
      all_args = cvd_common::Args{"cvd", "help"};
    }

    cvd_common::Envs envs =
        cvd_common::ConvertToEnvs(request.Message().command_request().env());

    auto subcmds = executor_.CmdList();
    auto selector_flag_collection =
        CF_EXPECT(selector::SelectorFlags::New()).FlagsAsCollection();

    FrontlineParser::ParserParam server_param{
        .server_supported_subcmds = subcmds,
        .internal_cmds = std::vector<std::string>{},
        .all_args = all_args,
        .cvd_flags = std::move(selector_flag_collection)};
    auto frontline_parser = CF_EXPECT(FrontlineParser::Parse(server_param));
    CF_EXPECT(frontline_parser != nullptr);

    const auto prog_path = frontline_parser->ProgPath();
    const auto new_sub_cmd = frontline_parser->SubCmd();
    cvd_common::Args cmd_args{frontline_parser->SubCmdArgs()};
    cvd_common::Args selector_args{frontline_parser->CvdArgs()};

    cvd_common::Args new_exec_args{prog_path};
    if (new_sub_cmd) {
      new_exec_args.push_back(*new_sub_cmd);
    }
    new_exec_args.insert(new_exec_args.end(), cmd_args.begin(), cmd_args.end());

    cvd::Request exec_request = MakeRequest(
        {.cmd_args = new_exec_args,
         .env = envs,
         .selector_args = selector_args,
         .working_dir =
             request.Message().command_request().working_directory()});

    RequestWithStdio forwarded_request(
        std::move(exec_request), request.FileDescriptors());
    SharedFD dev_null = SharedFD::Open("/dev/null", O_RDWR);
    CF_EXPECT(dev_null->IsOpen(), "Failed to open /dev/null");

    cvd::Response response;
    auto invocation_args =
        ParseInvocation(forwarded_request.Message()).arguments;
    auto handler = CF_EXPECT(executor_.GetHandler(forwarded_request));
    if (CF_EXPECT(IsHelpSubcmd(invocation_args)) &&
        handler->ShouldInterceptHelp()) {
      std::string output =
          CF_EXPECT(handler->DetailedHelp(invocation_args)) + "\n";
      response = CF_EXPECT(WriteToFd(forwarded_request.Out(), output));
    } else {
      response = CF_EXPECT(executor_.ExecuteOne(forwarded_request, dev_null));
    }
    return response;
  }

  // not intended to be used by the user
  cvd_common::Args CmdList() const override { return {}; }
  // not intended to show up in help
  Result<std::string> SummaryHelp() const override { return ""; }
  bool ShouldInterceptHelp() const override { return false; }
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return "";
  }

 private:
  CommandSequenceExecutor& executor_;
};

std::unique_ptr<CvdServerHandler> NewCvdServerHandlerProxy(
    CommandSequenceExecutor& executor) {
  return std::unique_ptr<CvdServerHandler>(new CvdServerHandlerProxy(executor));
}

}  // namespace cuttlefish
