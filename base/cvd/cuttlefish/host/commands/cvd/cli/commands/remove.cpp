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

#include "cuttlefish/host/commands/cvd/cli/commands/remove.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
RemoveCvdCommandHandler::RemoveCvdCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

cvd_common::Args RemoveCvdCommandHandler::CmdList() const {
  return {"remove", "rm"};
}

std::string RemoveCvdCommandHandler::SummaryHelp() const {
  return "Remove instance groups and related artifacts from the system.";
}

std::vector<HelpParagraph> RemoveCvdCommandHandler::Description() const {
  return {
      HelpParagraph(
          "Removal consists of deleting the group's runtime directory (which "
          "includes log files and virtual disks), and removing the group "
          "from the CVD instance database. This operation completely removes "
          "any trace of the group from the system and cannot be undone."),
      HelpParagraph(
          "The `cvd remove` command operates on entire instance groups. Even "
          "if you target a specific instance using selector flags (e.g., "
          "`--instance_name`), the entire group containing that instance will "
          "be removed."),
      HelpParagraph(
          "If the selected instance group is running, the command will attempt "
          "to stop it first. If the instances cannot be stopped, the removal "
          "will fail. In this case, you may need to run `cvd reset` to "
          "clean up the system."),
  };
}

bool RemoveCvdCommandHandler::RequiresDeviceExists() const { return true; }

Result<void> RemoveCvdCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> subcmd_args = request.SubcommandArguments();
  CF_EXPECT(
      ConsumeFlags({}, subcmd_args, {.fail_on_unexpected_argument = true}));

  if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
    return CF_ERR(NoGroupMessage(request));
  }
  auto group = CF_EXPECT(selector::SelectGroup(instance_manager_, request));

  Result<void> stop_res = StopGroup(group);
  if (!stop_res.ok()) {
    LOG(ERROR) << stop_res.error();
    LOG(ERROR) << "Unable to stop devices first, run `cvd reset` to forcibly "
                  "kill any remaining device processes.";
  }

  CF_EXPECT(instance_manager_.RemoveInstanceGroup(group));

  return {};
}

Result<void> RemoveCvdCommandHandler::StopGroup(
    LocalInstanceGroup& group) const {
  if (!group.HasActiveInstances()) {
    return {};
  }
  CF_EXPECT(instance_manager_.StopInstanceGroup(
      group, std::chrono::seconds(5), InstanceDirActionOnStop::Clear));
  return {};
}

std::unique_ptr<CvdCommandHandler> NewRemoveCvdCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new RemoveCvdCommandHandler(instance_manager));
}

}  // namespace cuttlefish
