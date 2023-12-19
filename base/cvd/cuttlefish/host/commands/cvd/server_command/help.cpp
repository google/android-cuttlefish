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
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/request_context.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

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
  -help                  Print this message
  -verbosity=<LEVEL>     Adjust Cvd verbosity level. LEVEL is Android log
                         severity. (Required: cvd >= v1.3)
  -acquire_file_lock     If the flag is given, the cvd server attempts to
                         acquire the instance lock file lock. (default: true)

Commands (cvd help <command> for more information):)";

class CvdHelpHandler : public CvdServerHandler {
 public:
  CvdHelpHandler(
      const std::vector<std::unique_ptr<CvdServerHandler>>& request_handlers)
      : request_handlers_(request_handlers) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return (invocation.command == "help");
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interruptible_);
    if (interrupted_) {
      return CF_ERR("Interrupted");
    }
    CF_EXPECT(CanHandle(request));

    cvd::Response response;
    response.mutable_command_response();  // Sets oneof member
    std::string output;

    auto args = ParseInvocation(request.Message()).arguments;
    if (args.empty()) {
      output = CF_EXPECT(TopLevelHelp());
    } else {
      output = CF_EXPECT(SubCommandHelp(args));
    }

    auto written_size = WriteAll(request.Out(), output);
    CF_EXPECT_EQ(output.size(), written_size, request.Out()->StrError());

    interrupt_lock.unlock();
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interruptible_);
    interrupted_ = true;
    return {};
  }

  cvd_common::Args CmdList() const override { return {"help"}; }

 private:
  Result<RequestWithStdio> GetLookupRequest(const std::string& arg) {
    cvd::Request lookup;
    auto& lookup_cmd = *lookup.mutable_command_request();
    lookup_cmd.add_args("cvd");
    lookup_cmd.add_args(arg);
    auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
    CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
    return RequestWithStdio(dev_null, lookup, {dev_null, dev_null, dev_null},
                            {});
  }

  Result<std::string> TopLevelHelp() {
    std::stringstream help_message;
    help_message << kHelpMessage << std::endl;
    for (const auto& handler : request_handlers_) {
      std::string command_list = android::base::Join(handler->CmdList(), ", ");
      // exclude commands without any command list values as not intended for
      // use by users or sub-subcommands
      if (!command_list.empty()) {
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
    auto lookup_request = CF_EXPECT(GetLookupRequest(args.front()));
    auto handler = CF_EXPECT(RequestHandler(lookup_request, request_handlers_));

    std::stringstream help_message;
    help_message << CF_EXPECT(handler->DetailedHelp(args)) << std::endl;
    return help_message.str();
  }

  std::mutex interruptible_;
  bool interrupted_ = false;
  const std::vector<std::unique_ptr<CvdServerHandler>>& request_handlers_;
};

std::unique_ptr<CvdServerHandler> NewCvdHelpHandler(
    const std::vector<std::unique_ptr<CvdServerHandler>>& request_handlers) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdHelpHandler(request_handlers));
}

}  // namespace cuttlefish
