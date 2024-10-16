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

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/status_fetcher.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Query status of a single instance group.  Use `cvd fleet` for all devices";

constexpr char kDetailedHelpText[] = R"(

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

}  // namespace

class CvdStatusCommandHandler : public CvdServerHandler {
 public:
  CvdStatusCommandHandler(InstanceManager& instance_manager,
                          HostToolTargetManager& host_tool_target_manager);

  Result<bool> CanHandle(const CommandRequest& request) const override;
  Result<cvd::Response> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override;

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  InstanceManager& instance_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  StatusFetcher status_fetcher_;
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
    const CommandRequest& request) const {
  auto invocation = ParseInvocation(request);
  return Contains(supported_subcmds_, invocation.command);
}

static Result<CommandRequest> ProcessInstanceNameFlag(
    const CommandRequest& request) {
  cvd_common::Envs env = request.Env();
  auto [subcmd, cmd_args] = ParseInvocation(request);

  CvdFlag<std::string> instance_name_flag("instance_name");
  auto instance_name_flag_opt =
      CF_EXPECT(instance_name_flag.FilterFlag(cmd_args));

  if (!instance_name_flag_opt) {
    return request;
  }

  std::string internal_name_or_id = *instance_name_flag_opt;
  int id;
  if (android::base::ParseInt(internal_name_or_id, &id)) {
    env[kCuttlefishInstanceEnvVarName] = std::to_string(id);
  } else {
    CF_EXPECT(android::base::StartsWith(internal_name_or_id, kCvdNamePrefix),
              "--instance_name should be either cvd-<id> or id");
    std::string id_part =
        internal_name_or_id.substr(std::string(kCvdNamePrefix).size());
    CF_EXPECT(android::base::ParseInt(id_part, &id),
              "--instance_name should be either cvd-<id> or id");
    env[kCuttlefishInstanceEnvVarName] = std::to_string(id);
  }

  return CF_EXPECT(CommandRequestBuilder()
                       .AddArguments({"cvd", "status"})
                       .AddArguments(cmd_args)
                       .SetEnv(std::move(env))
                       .AddSelectorArguments(request.Selectors().AsArgs())
                       .Build());
}

static Result<bool> HasPrint(cvd_common::Args cmd_args) {
  CvdFlag<bool> print_flag("print");
  auto print_flag_opt = CF_EXPECT(print_flag.FilterFlag(cmd_args));
  return print_flag_opt.has_value() && *print_flag_opt;
}

Result<cvd::Response> CvdStatusCommandHandler::Handle(
    const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));

  auto [subcmd, cmd_args] = ParseInvocation(request);
  CF_EXPECT(Contains(supported_subcmds_, subcmd));
  const bool has_print = CF_EXPECT(HasPrint(cmd_args));

  if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
    return NoGroupResponse(request);
  }
  CommandRequest new_request = CF_EXPECT(ProcessInstanceNameFlag(request));

  auto [entire_stderr_msg, instances_json, response] =
      CF_EXPECT(status_fetcher_.FetchStatus(new_request));
  if (response.status().code() != cvd::Status::OK) {
    return response;
  }

  std::string serialized_group_json = instances_json.toStyledString();
  std::cerr << serialized_group_json;
  if (has_print) {
    std::cout << serialized_group_json;
  }
  return response;
}

std::vector<std::string> CvdStatusCommandHandler::CmdList() const {
  return supported_subcmds_;
}

std::unique_ptr<CvdServerHandler> NewCvdStatusCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdStatusCommandHandler(instance_manager, host_tool_target_manager));
}

}  // namespace cuttlefish
