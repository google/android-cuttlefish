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

#include "host/commands/cvd/server_command/status.h"

#include <sys/types.h>

#include <mutex>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/status_fetcher.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {

static constexpr char kHelpMessage[] = R"(

usage: cvd <selector/driver options> <command> <args>

Selector Options:
  -group_name <name>     Specify the name of the instance group created
                         or selected.
  -instance_name <name>  Selects the device of the given name to perform the
                         commands for.
  -instance_name <names> Takes the names of the devices to create within an
                         instance group. The 'names' is comma-separated.

Driver Options:
  -verbosity=<LEVEL>     Adjust Cvd verbosity level. LEVEL is Android log
                         severity. (Required: cvd >= v1.3)

Args:
  --wait_for_launcher    How many seconds to wait for the launcher to respond
                         to the status command. A value of zero means wait
                         indefinitely
                         (Current value: "5")

  --instance_name        Either instance id (e.g. 1) or internal name (e.g.
                         cvd-1) If not provided, the smallest id in the given
                         instance group is selected.
                         (Current value: "", Required: Android > 12)

  --print                If provided, prints status and instance config
                         information to stdout instead of CHECK.
                         (Current value: "false", Required: Android > 12)

  --all_instances        List, within the given instance group, all instances
                         status and instance config information.
                         (Current value: "false", Required: Android > 12)

  --help                 List this message

  *                      Only the flags in `-help` are supported. Positional
                         arguments are not supported.

)";

class CvdStatusCommandHandler : public CvdServerHandler {
 public:
  CvdStatusCommandHandler(InstanceManager& instance_manager,
                          HostToolTargetManager& host_tool_target_manager);

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  cvd_common::Args CmdList() const override;

 private:
  Result<cvd::Response> HandleHelp(const RequestWithStdio&);

  InstanceManager& instance_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  StatusFetcher status_fetcher_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  std::vector<std::string> supported_subcmds_;
};

CvdStatusCommandHandler::CvdStatusCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager)
    : instance_manager_(instance_manager),
      host_tool_target_manager_(host_tool_target_manager),
      status_fetcher_(instance_manager_, host_tool_target_manager_),
      supported_subcmds_{"status", "cvd_status"} {}

Result<bool> CvdStatusCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(supported_subcmds_, invocation.command);
}

Result<void> CvdStatusCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(status_fetcher_.Interrupt());
  return {};
}

static Result<RequestWithStdio> ProcessInstanceNameFlag(
    const RequestWithStdio& request) {
  cvd_common::Envs envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  auto [subcmd, cmd_args] = ParseInvocation(request.Message());

  CvdFlag<std::string> instance_name_flag("instance_name");
  auto instance_name_flag_opt =
      CF_EXPECT(instance_name_flag.FilterFlag(cmd_args));

  if (!instance_name_flag_opt) {
    return request;
  }

  std::string internal_name_or_id = *instance_name_flag_opt;
  int id;
  if (android::base::ParseInt(internal_name_or_id, &id)) {
    envs[kCuttlefishInstanceEnvVarName] = std::to_string(id);
  } else {
    CF_EXPECT(android::base::StartsWith(internal_name_or_id, kCvdNamePrefix),
              "--instance_name should be either cvd-<id> or id");
    std::string id_part =
        internal_name_or_id.substr(std::string(kCvdNamePrefix).size());
    CF_EXPECT(android::base::ParseInt(id_part, &id),
              "--instance_name should be either cvd-<id> or id");
    envs[kCuttlefishInstanceEnvVarName] = std::to_string(id);
  }

  cvd_common::Args new_cmd_args{"cvd", "status"};
  new_cmd_args.insert(new_cmd_args.end(), cmd_args.begin(), cmd_args.end());
  const auto& selector_opts =
      request.Message().command_request().selector_opts();
  cvd::Request new_message = MakeRequest({
      .cmd_args = new_cmd_args,
      .env = envs,
      .selector_args = cvd_common::ConvertToArgs(selector_opts.args()),
      .working_dir = request.Message().command_request().working_directory(),
  });
  return RequestWithStdio(request.Client(), new_message,
                          request.FileDescriptors(), request.Credentials());
}

static Result<bool> HasPrint(cvd_common::Args cmd_args) {
  CvdFlag<bool> print_flag("print");
  auto print_flag_opt = CF_EXPECT(print_flag.FilterFlag(cmd_args));
  return print_flag_opt.has_value() && *print_flag_opt;
}

Result<cvd::Response> CvdStatusCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(request.Credentials());

  auto precondition_verified = VerifyPrecondition(request);
  if (!precondition_verified.ok()) {
    cvd::Response response;
    response.mutable_command_response();
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(
        precondition_verified.error().Message());
    return response;
  }

  CF_EXPECT_NE(request.Message().command_request().wait_behavior(),
               cvd::WAIT_BEHAVIOR_START,
               "cvd status shouldn't be cvd::WAIT_BEHAVIOR_START");
  interrupt_lock.unlock();

  auto [subcmd, cmd_args] = ParseInvocation(request.Message());
  CF_EXPECT(Contains(supported_subcmds_, subcmd));
  const bool has_print = CF_EXPECT(HasPrint(cmd_args));

  if (CF_EXPECT(IsHelpSubcmd(cmd_args))) {
    return HandleHelp(request);
  }

  if (instance_manager_.AllGroupNames().empty()) {
    return CF_EXPECT(NoGroupResponse(request));
  }
  RequestWithStdio new_request = CF_EXPECT(ProcessInstanceNameFlag(request));

  auto [entire_stderr_msg, instances_json, response] =
      CF_EXPECT(status_fetcher_.FetchStatus(new_request));
  if (response.status().code() != cvd::Status::OK) {
    return response;
  }

  std::string serialized_group_json = instances_json.toStyledString();
  CF_EXPECT_EQ(WriteAll(request.Err(), entire_stderr_msg),
               entire_stderr_msg.size());
  if (has_print) {
    CF_EXPECT_EQ(WriteAll(request.Out(), serialized_group_json),
                 serialized_group_json.size());
  }
  return response;
}

std::vector<std::string> CvdStatusCommandHandler::CmdList() const {
  return supported_subcmds_;
}

Result<cvd::Response> CvdStatusCommandHandler::HandleHelp(
    const RequestWithStdio& request) {
  cvd::Response response;
  response.mutable_command_response();  // Sets oneof member
  response.mutable_status()->set_code(cvd::Status::OK);
  CF_EXPECT_EQ(WriteAll(request.Out(), kHelpMessage),
               strnlen(kHelpMessage, sizeof(kHelpMessage) - 1));
  return response;
}

std::unique_ptr<CvdServerHandler> NewCvdStatusCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdStatusCommandHandler(instance_manager, host_tool_target_manager));
}

}  // namespace cuttlefish
