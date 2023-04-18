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

#include "host/commands/cvd/server.h"

#include <mutex>

#include "common/libs/fs/shared_buf.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/inject.h"

namespace cuttlefish {

static constexpr char kHelpMessage[] = R"(Cuttlefish Virtual Device (CVD) CLI.

usage: cvd <selector/driver options> <command> <args>

Selector Options:
  -group_name <name>     Specify the name of the instance group created
                         or selected.
  -instance_name <name>  Selects the device of the given name to perform the
                         commands for.
  -instance_name <names> Takes the names of the devices to create within an
                         instance group. The 'names' is comma-separated.

Driver Options:
  --help                 Print this message
  -disable_default_group If the flag is true, the group's runtime files are
                         not populated under the user's HOME. Instead the
                         files are created under an automatically-generated
                         directory. (default: false)
  -acquire_file_lock     If the flag is given, the cvd server attempts to
                         acquire the instance lock file lock. (default: true)

Commands:
  help                   Print this message.
  help <command>         Print help for a command.
  start                  Start a device.
  stop                   Stop a running device.
  clear                  Stop all running devices and delete all instance and
                         assembly directories.
  fleet                  View the current fleet status.
  kill-server            Kill the cvd_server background process.
  server-kill            Same as kill-server
  restart-server         Restart the cvd_server background process.
  status                 Check and print the state of a running instance.
  host_bugreport         Capture a host bugreport, including configs, logs, and
                         tombstones.

Args:
  <command args>         Each command has its own set of args.
                         See cvd help <command>.

Experimental:
  reset                  See cvd reset --help. Requires cvd >= v1.2
)";

class CvdHelpHandler : public CvdServerHandler {
 public:
  INJECT(CvdHelpHandler(CommandSequenceExecutor& executor))
      : executor_(executor) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return (invocation.command == "help");
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interruptible_);
    if (interrupted_) {
      return CF_ERR("Interrupted");
    }

    cvd::Response response;
    response.mutable_command_response();  // Sets oneof member
    response.mutable_status()->set_code(cvd::Status::OK);

    CF_EXPECT(CanHandle(request));

    auto [subcmd, subcmd_args] = ParseInvocation(request.Message());
    const auto supported_subcmd_list = executor_.CmdList();

    /*
     * cvd help, cvd help invalid_token, cvd help help
     */
    if (subcmd_args.empty() ||
        !Contains(supported_subcmd_list, subcmd_args.front()) ||
        subcmd_args.front() == "help") {
      WriteAll(request.Out(), kHelpMessage);
      return response;
    }

    cvd::Request modified_proto = HelpSubcommandToFlag(request);

    RequestWithStdio inner_cmd(request.Client(), modified_proto,
                               request.FileDescriptors(),
                               request.Credentials());

    interrupt_lock.unlock();
    executor_.Execute({inner_cmd}, SharedFD::Open("/dev/null", O_RDWR));

    return response;
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interruptible_);
    interrupted_ = true;
    CF_EXPECT(executor_.Interrupt());
    return {};
  }

  cvd_common::Args CmdList() const override { return {"help"}; }

 private:
  cvd::Request HelpSubcommandToFlag(const RequestWithStdio& request);

  std::mutex interruptible_;
  bool interrupted_ = false;
  CommandSequenceExecutor& executor_;
};

cvd::Request CvdHelpHandler::HelpSubcommandToFlag(
    const RequestWithStdio& request) {
  cvd::Request modified_proto = request.Message();
  auto all_args =
      cvd_common::ConvertToArgs(modified_proto.command_request().args());
  auto& args = *modified_proto.mutable_command_request()->mutable_args();
  args.Clear();
  // there must be one or more "help" in all_args
  // delete the first "help"
  bool found_help = false;
  for (const auto& cmd_arg : all_args) {
    if (cmd_arg != "help" || found_help) {
      args.Add(cmd_arg.c_str());
      continue;
    }
    // skip first help
    found_help = true;
  }
  args.Add("--help");
  return modified_proto;
}

fruit::Component<fruit::Required<CommandSequenceExecutor>> CvdHelpComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdHelpHandler>();
}

}  // namespace cuttlefish
