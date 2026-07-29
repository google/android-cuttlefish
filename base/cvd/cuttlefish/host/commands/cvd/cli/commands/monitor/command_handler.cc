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
#include <string_view>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/utils/interrupt_listener.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

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
  cvd [selector options] monitor [flags]
)";

constexpr char kMonitorCmd[] = "monitor";

constexpr char kSeverityHelp[] =
    "Set the severity cutoff of the monitor. Supported values are error, "
    "warning, info, debug, and verbose";
constexpr char kErrorHelp[] = "Equivalent to --monitor_severity=error";

}  // namespace

CvdMonitorCommandHandler::CvdMonitorCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_{instance_manager} {
  flags_.severity_ = ConsoleSeverity();
}

std::vector<std::string> CvdMonitorCommandHandler::CmdList() const {
  return {kMonitorCmd};
}

std::vector<HelpParagraph> CvdMonitorCommandHandler::Description() const {
  return {HelpParagraph::Raw(kDetailedHelpText)};
}

Result<std::vector<Flag>> CvdMonitorCommandHandler::Flags(
    const CommandRequest&) {
  auto set_severity = [this](std::string_view severity) -> Result<void> {
    flags_.severity_ = CF_EXPECT(ToSeverity(severity));
    return {};
  };
  auto set_error = [this](std::string_view) -> Result<void> {
    flags_.severity_ = LogSeverity::Error;
    return {};
  };
  return std::vector<Flag>{
      Flag::StringFlag("severity").Setter(set_severity).Help(kSeverityHelp),
      Flag::StringFlag("verbosity").Setter(set_severity).Help(kSeverityHelp),
      Flag::BoolFlag("e").Setter(set_error).Help(kErrorHelp),
  };
}

Result<void> CvdMonitorCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(isatty(0), "The monitor command requires an interactive terminal.");

  std::vector<std::string> args = request.SubcommandArguments();
  std::vector<Flag> flags = CF_EXPECT(Flags(request));
  CF_EXPECT(ConsumeFlags(flags, args, {.fail_on_unexpected_argument = true}));

  const auto [instance, unused] =
      CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                "Unable to select an instance");

  SharedFD stop_eventfd = SharedFD::Event();
  CF_EXPECTF(stop_eventfd->IsOpen(),
             "Failed to create eventfd for stopping monitor: {}",
             stop_eventfd->StrError());

  std::unique_ptr<InterruptListenerHandle> stop_listener =
      CF_EXPECT(PushInterruptListener(
          [stop_eventfd](int) { stop_eventfd->EventfdWrite(1); }));

  CF_EXPECT(MonitorLogs(instance, stop_eventfd, flags_.severity_));

  return {};
}

std::string CvdMonitorCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdMonitorCommandHandler::RequiresDeviceExists() const { return true; }

std::unique_ptr<CvdCommandHandler> NewCvdMonitorCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdMonitorCommandHandler(instance_manager));
}

}  // namespace cuttlefish
