/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/command_handler.h"

#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

constexpr char kSummaryHelpText[] =
    "Monitor device logs (launcher, kernel, and logcat) in real-time.";
constexpr char kDetailedHelpText[] =
    R"(monitor: Monitors a particular device by displaying the last 10 lines of its logs.
It requires an interactive terminal and will continuously update the display every 50ms.

It displays:
- launcher.log
- kernel.log
- logcat

Usage:
  cvd [selector options] monitor
)";

constexpr char kMonitorCmd[] = "monitor";

}  // namespace

CvdMonitorCommandHandler::CvdMonitorCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_{instance_manager} {}

Result<void> CvdMonitorCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(isatty(0), "The monitor command requires an interactive terminal.");

  std::vector<std::string> args = request.SubcommandArguments();
  CF_EXPECT(ConsumeFlags({}, args, {.fail_on_unexpected_argument = true}));

  const auto [instance, unused] =
      CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                "Unable to select an instance");

  return MonitorLogs(instance);
}

cvd_common::Args CvdMonitorCommandHandler::CmdList() const {
  return {kMonitorCmd};
}

std::string CvdMonitorCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdMonitorCommandHandler::RequiresDeviceExists() const { return true; }

Result<std::string> CvdMonitorCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  return kDetailedHelpText;
}

std::unique_ptr<CvdCommandHandler> NewCvdMonitorCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdMonitorCommandHandler(instance_manager));
}

}  // namespace cuttlefish
