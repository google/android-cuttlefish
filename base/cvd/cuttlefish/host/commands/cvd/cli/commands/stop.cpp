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

#include "cuttlefish/host/commands/cvd/cli/commands/stop.h"

#include <signal.h>  // IWYU pragma: keep
#include <stdlib.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/libs/metrics/device_metrics_orchestration.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "Stop Cuttlefish instances";

struct StopFlags {
  size_t wait_for_launcher_secs = 5;
  bool clear_instance_dirs = false;
};
}  // namespace

CvdStopCommandHandler::CvdStopCommandHandler(InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<void> CvdStopCommandHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> cmd_args = request.SubcommandArguments();

  if (!CF_EXPECT(instance_manager_.HasInstanceGroups())) {
    return CF_ERR(NoGroupMessage(request));
  }

  auto group = CF_EXPECT(selector::SelectGroup(instance_manager_, request));
  CF_EXPECT(group.HasActiveInstances(), "Selected group is not running");

  CF_EXPECT(ConsumeFlags(CF_EXPECT(Flags(request)), cmd_args,
                         {.fail_on_unexpected_argument = true}));
  std::optional<std::chrono::seconds> launcher_timeout;
  if (flags_.wait_for_launcher_secs > 0) {
    launcher_timeout.emplace(flags_.wait_for_launcher_secs);
  }

  std::vector<unsigned> instance_nums;
  if (request.Selectors().instance_names) {
    for (const auto& name : *request.Selectors().instance_names) {
      std::vector<LocalInstance> instances = group.FindByInstanceName(name);
      CF_EXPECTF(!instances.empty(), "Instance '{}' not found in group '{}'",
                 name, group.GroupName());
      for (const auto& inst : instances) {
        instance_nums.push_back(inst.Id());
      }
    }
  }

  Result<void> stop_outcome = instance_manager_.StopInstanceGroup(
      group, launcher_timeout,
      flags_.clear_instance_dirs ? InstanceDirActionOnStop::Clear
                                 : InstanceDirActionOnStop::Keep,
      instance_nums);

  GatherVmStopMetrics(group);

  CF_EXPECT(std::move(stop_outcome));
  return {};
}

cvd_common::Args CvdStopCommandHandler::CmdList() const {
  return {"stop", "stop_cvd"};
}

std::string CvdStopCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

std::vector<HelpParagraph> CvdStopCommandHandler::Description() const {
  std::vector<HelpParagraph> description;
  description.emplace_back(
      HelpParagraph::Raw("Usage:\n    cvd [selectors] stop [args]"));
  description.emplace_back(
      "Stop a subset of instances from a group. A single instance, several or "
      "all instances in a group can be stopped at once. To stop instances from "
      "different groups the command must be invoked multiple times.");
  description.emplace_back(
      "Instances must be in 'Running' or 'Starting' states, otherwise the "
      "command will fail. Instances will be left in 'Stopped' state if the "
      "command succeeds and can later be started with the `cvd start` "
      "command.");
  description.emplace_back(
      "Instances are stopped by asking the virtual machine manager to stop, "
      "which typically means immediately stopping all VCPU threads. This may "
      "lead to data loss and/or corruption as it's roughly equivalent to "
      "removing the battery from a physical Android device. Logs, virtual "
      "disks and other files are preserved after a stop completes unless "
      "--clear_instance_dirs is given.");
  return description;
}

Result<std::vector<Flag>> CvdStopCommandHandler::Flags(const CommandRequest&) {
  return std::vector<Flag>{
      GflagsCompatFlag("wait_for_launcher_seconds",
                       flags_.wait_for_launcher_secs)
          .Alias("wait_for_launcher")
          .Help("Number of seconds to wait for the running instance(s) "
                "to report that it stopped successfully before "
                "forcefully stopping it."),
      GflagsCompatFlag("clear_instance_dirs", flags_.clear_instance_dirs)
          .Help("Deletes log files, temporary files, virtual disks "
                "overlays and other instance specific state. It does "
                "not delete the original disk images, but reverts any "
                "changes the instance may have written to disk."),
  };
}

std::unique_ptr<CvdCommandHandler> NewCvdStopCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdStopCommandHandler(instance_manager));
}

}  // namespace cuttlefish
