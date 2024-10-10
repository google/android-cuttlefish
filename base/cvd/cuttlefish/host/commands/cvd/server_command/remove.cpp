/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/cvd/server_command/remove.h"

#include <memory>
#include <string>
#include <vector>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/group_selector.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"

namespace cuttlefish {
namespace {

Result<cvd::Response> Success() {
  cvd::Response ok_response;
  ok_response.mutable_command_response();
  auto& status = *ok_response.mutable_status();
  status.set_code(cvd::Status::OK);
  return ok_response;
}

}  // namespace

class RemoveCvdCommandHandler : public CvdServerHandler {
 public:
  RemoveCvdCommandHandler(InstanceManager& instance_manager)
      : instance_manager_(instance_manager) {}

  cvd_common::Args CmdList() const override { return {"remove", "rm"}; }

  Result<std::string> SummaryHelp() const override {
    return "Remove devices and artifacts from the system.";
  }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return "Removes selected devices from the system.\n\n"
           "Running devices are stopped first. Deletes build and runtime "
           "artifacts, including log files and images (only if downloaded by "
           "cvd itself)";
  }

  bool ShouldInterceptHelp() const override { return false; }

  Result<bool> CanHandle(const CommandRequest& request) const override {
    auto invocation = ParseInvocation(request);
    return Contains(CmdList(), invocation.command);
  }

  Result<cvd::Response> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));
    auto [op, subcmd_args] = ParseInvocation(request);

    if (CF_EXPECT(IsHelpSubcmd(subcmd_args))) {
      std::vector<std::string> unused;
      std::cout << CF_EXPECT(DetailedHelp(unused));
      return Success();
    }

    if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
      return NoGroupResponse(request);
    }
    auto group = CF_EXPECT(SelectGroup(instance_manager_, request));

    auto stop_res = StopGroup(group, request);
    if (!stop_res.ok()) {
      LOG(ERROR) << stop_res.error().FormatForEnv();
      LOG(ERROR) << "Unable to stop devices first, run `cvd reset` to forcibly "
                    "kill any remaining device processes.";
    }

    CF_EXPECT(instance_manager_.RemoveInstanceGroupByHome(group.HomeDir()));

    return Success();
  }

 private:
  Result<void> StopGroup(selector::LocalInstanceGroup& group,
                         const CommandRequest& request) const {
    if (!group.HasActiveInstances()) {
      return {};
    }
    auto config_path =
        CF_EXPECT(selector::GetCuttlefishConfigPath(group.HomeDir()));
    CF_EXPECT(instance_manager_.IssueStopCommand(request, config_path, group));
    return {};
  }

  Result<void> HelpCommand(const CommandRequest& request) const {
    std::vector<std::string> unused;
    std::string msg = CF_EXPECT(DetailedHelp(unused));
    std::cout << msg;
    return {};
  }

  InstanceManager& instance_manager_;
};

std::unique_ptr<CvdServerHandler> NewRemoveCvdCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new RemoveCvdCommandHandler(instance_manager));
}

}  // namespace cuttlefish
