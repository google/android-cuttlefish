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

#include <mutex>
#include <vector>

#include <android-base/strings.h>

#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/frontline_parser.h"
#include "host/commands/cvd/metrics/cvd_metrics_api.h"
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
    std::unique_lock interrupt_lock(interruptible_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));

    const auto& selector_opts =
        request.Message().command_request().selector_opts();
    auto all_args = cvd_common::ConvertToArgs(selector_opts.args());
    CF_EXPECT_GE(all_args.size(), 1);
    if (all_args.size() == 1) {
      CF_EXPECT_EQ(all_args.front(), "cvd");
      all_args = cvd_common::Args{"cvd", "help"};
    }

    cvd_common::Envs envs =
        cvd_common::ConvertToEnvs(request.Message().command_request().env());

    auto subcmds = executor_.CmdList();
    auto selector_flag_collection =
        selector::SelectorFlags::New().FlagsAsCollection();

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
    CvdMetrics::SendCvdMetrics(new_exec_args);

    cvd::Request exec_request = MakeRequest(
        {.cmd_args = new_exec_args,
         .env = envs,
         .selector_args = selector_args,
         .working_dir =
             request.Message().command_request().working_directory()},
        request.Message().command_request().wait_behavior());

    RequestWithStdio forwarded_request(
        request.Client(), std::move(exec_request), request.FileDescriptors(),
        request.Credentials());
    interrupt_lock.unlock();
    SharedFD dev_null = SharedFD::Open("/dev/null", O_RDWR);
    CF_EXPECT(dev_null->IsOpen(), "Failed to open /dev/null");
    const auto responses =
        CF_EXPECT(executor_.Execute({std::move(forwarded_request)}, dev_null));
    CF_EXPECT_EQ(responses.size(), 1);
    // TODO(moelsherif): check the response for failed command
    return responses.front();
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interruptible_);
    interrupted_ = true;
    CF_EXPECT(executor_.Interrupt());
    return {};
  }

  // not intended to be used by the user
  cvd_common::Args CmdList() const override { return {}; }

 private:
  std::mutex interruptible_;
  bool interrupted_ = false;
  CommandSequenceExecutor& executor_;
};

std::unique_ptr<CvdServerHandler> NewCvdServerHandlerProxy(
    CommandSequenceExecutor& executor) {
  return std::unique_ptr<CvdServerHandler>(new CvdServerHandlerProxy(executor));
}

}  // namespace cuttlefish
