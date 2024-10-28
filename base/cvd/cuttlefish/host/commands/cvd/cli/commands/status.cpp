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

#include "host/commands/cvd/cli/commands/status.h"

#include <sys/types.h>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/legacy/cvd_server.pb.h"
#include "host/commands/cvd/cli/commands/server_handler.h"
#include "host/commands/cvd/cli/group_selector.h"
#include "host/commands/cvd/cli/selector/device_selector_utils.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/instances/instance_manager.h"
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
                         to the status request. A value of zero means wait
                         indefinitely.
                         (Current value: "5")

  --instance_name        Deprecated, use selectors instead.

  --print                If provided, prints status and instance config
                         information to stdout instead of CHECK.
                         (Current value: "false", Required: Android > 12)

  --help                 List this message

)";

Result<unsigned> IdFromInstanceNameFlag(std::string_view name_or_id) {
  android::base::ConsumePrefix(&name_or_id, kCvdNamePrefix);
  unsigned id;
  CF_EXPECT(android::base::ParseUint(std::string(name_or_id), &id),
            "--instance_name should be either cvd-<id> or id. To use it as a "
            "selector flag it must appear before the subcommand.");
  return id;
}

struct StatusCommandOptions {
  int wait_for_launcher_seconds;
  std::string instance_name;
  bool print;
  bool help;
};

Result<StatusCommandOptions> ParseFlags(cvd_common::Args& args) {
  StatusCommandOptions ret{
      .wait_for_launcher_seconds = 5,
      .instance_name = "",
      .print = false,
      .help = false,
  };
  std::vector<Flag> flags = {
      GflagsCompatFlag("wait_for_launcher", ret.wait_for_launcher_seconds),
      GflagsCompatFlag("instance_name", ret.instance_name),
      GflagsCompatFlag("print", ret.print),
      GflagsCompatFlag("help", ret.help),
  };

  CF_EXPECT(ConsumeFlags(flags, args));

  return ret;
}

}  // namespace

class CvdStatusCommandHandler : public CvdServerHandler {
 public:
  CvdStatusCommandHandler(InstanceManager& instance_manager);

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
  std::vector<std::string> supported_subcmds_;
};

CvdStatusCommandHandler::CvdStatusCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager),
      supported_subcmds_{"status", "cvd_status"} {}

Result<bool> CvdStatusCommandHandler::CanHandle(
    const CommandRequest& request) const {
  auto invocation = ParseInvocation(request);
  return Contains(supported_subcmds_, invocation.command);
}

Result<cvd::Response> CvdStatusCommandHandler::Handle(
    const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));

  auto [subcmd, cmd_args] = ParseInvocation(request);
  CF_EXPECT(Contains(supported_subcmds_, subcmd));
  StatusCommandOptions flags = CF_EXPECT(ParseFlags(cmd_args));

  if (flags.help) {
    std::cout << kDetailedHelpText << std::endl;
    return SuccessResponse();
  }

  if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
    return NoGroupResponse(request);
  }

  if (request.Selectors().instance_names && !flags.instance_name.empty()) {
    return CF_ERR(
        "The subcommand flag '--instance_name' conflicts with the selector "
        "flag of the same name and can't be used at the same time.");
  }

  Json::Value status_array(Json::arrayValue);

  if (!request.Selectors().instance_names && flags.instance_name.empty()) {
    // No attempt at selecting an instance, get group status instead
    LocalInstanceGroup group =
        CF_EXPECT(SelectGroup(instance_manager_, request));
    status_array = CF_EXPECT(group.FetchStatus(
        std::chrono::seconds(flags.wait_for_launcher_seconds)));
    instance_manager_.UpdateInstanceGroup(group);
  } else {
    std::pair<LocalInstance, LocalInstanceGroup> pair =
        flags.instance_name.empty()
            ? CF_EXPECT(instance_manager_.SelectInstance(
                  CF_EXPECT(selector::BuildFilterFromSelectors(
                      request.Selectors(), request.Env()))))
            : CF_EXPECT(instance_manager_.FindInstanceById(
                  CF_EXPECT(IdFromInstanceNameFlag(flags.instance_name))));
    LocalInstance instance = pair.first;
    LocalInstanceGroup group = pair.second;
    status_array.append(CF_EXPECT(instance.FetchStatus(
        std::chrono::seconds(flags.wait_for_launcher_seconds))));
    instance_manager_.UpdateInstanceGroup(group);
  }

  if (flags.print) {
    std::cout << status_array.toStyledString();
  }

  return SuccessResponse();
}

std::vector<std::string> CvdStatusCommandHandler::CmdList() const {
  return supported_subcmds_;
}

std::unique_ptr<CvdServerHandler> NewCvdStatusCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdStatusCommandHandler(instance_manager));
}

}  // namespace cuttlefish
