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

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/ansi_codes.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/display.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/libs/log_names/log_names.h"
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

void ClearLastNLines(int n) {
  if (n > 0) {
    // Move cursor up N lines and clear to end of screen
    std::cout << AnsiCursorUp(n) << kAnsiClearScreen << std::flush;
  }
}

}  // namespace

CvdMonitorCommandHandler::CvdMonitorCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_{instance_manager} {}

Result<void> CvdMonitorCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(isatty(0), "The monitor command requires an interactive terminal.");

  std::vector<std::string> args = request.SubcommandArguments();
  CF_EXPECT(ConsumeFlags({UnexpectedArgumentGuard()}, args));

  auto [instance, unused] =
      CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                "Unable to select an instance");

  std::string kernel_log =
      absl::StrCat(instance.instance_dir(), "/logs/", kLogNameKernel);
  std::string launcher_log =
      absl::StrCat(instance.instance_dir(), "/logs/", kLogNameLauncher);
  std::string logcat =
      absl::StrCat(instance.instance_dir(), "/logs/", kLogNameLogcat);

  SharedFD kernel_fd;
  SharedFD launcher_fd;
  SharedFD logcat_fd;

  while (true) {
    if (!kernel_fd->IsOpen()) {
      kernel_fd = SharedFD::Open(kernel_log, O_RDONLY);
    }
    if (!launcher_fd->IsOpen()) {
      launcher_fd = SharedFD::Open(launcher_log, O_RDONLY);
    }
    if (!logcat_fd->IsOpen()) {
      logcat_fd = SharedFD::Open(logcat, O_RDONLY);
    }

    Result<TerminalSize> term_size_result = GetTerminalSize();
    int width = 79;  // Default fallback width (80 - 1)
    if (term_size_result.ok()) {
      width = term_size_result->columns - 1;
    }
    LogMonitorDisplay display(width);

    display.DrawFile(launcher_fd, kLogNameLauncher);
    display.DrawFile(kernel_fd, kLogNameKernel);
    display.DrawFile(logcat_fd, kLogNameLogcat);

    std::cout << display.Finalize() << std::flush;

    // Wait a bit before clearing and redrawing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ClearLastNLines(display.TotalLinesDrawn());
  }

  return {};
}

cvd_common::Args CvdMonitorCommandHandler::CmdList() const {
  return {kMonitorCmd};
}

std::string CvdMonitorCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdMonitorCommandHandler::RequiresDeviceExists() const { return true; }

Result<std::string> CvdMonitorCommandHandler::DetailedHelp(
    const CommandRequest& request) const {
  return kDetailedHelpText;
}

std::unique_ptr<CvdCommandHandler> NewCvdMonitorCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdMonitorCommandHandler(instance_manager));
}

}  // namespace cuttlefish
