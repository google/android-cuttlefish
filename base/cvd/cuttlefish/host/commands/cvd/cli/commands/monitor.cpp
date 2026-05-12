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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor.h"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/libs/log_names/log_names.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

Result<std::vector<std::string>> GetLastNLines(SharedFD fd, size_t n) {
  off_t file_size = fd->LSeek(0, SEEK_END);
  CF_EXPECT(file_size != -1, "Failed to seek to end of file");

  absl::Cord accumulated_data;
  off_t offset = file_size;
  size_t newline_count = 0;

  while (offset > 0 && newline_count < n + 1) {
    static constexpr off_t kChunkSize = 4096;
    size_t to_read = std::min(kChunkSize, offset);
    offset -= to_read;
    fd->LSeek(offset, SEEK_SET);

    std::string chunk(to_read, '\0');
    ssize_t bytes_read = ReadExact(fd, &chunk);
    CF_EXPECTF(bytes_read == static_cast<ssize_t>(to_read), "Read failed: '{}'",
               fd->StrError());

    newline_count += std::count(chunk.begin(), chunk.end(), '\n');
    accumulated_data.Prepend(std::move(chunk));
  }

  std::vector<std::string> all_lines =
      absl::StrSplit(std::string(accumulated_data), '\n');

  // Handle trailing newline
  if (!all_lines.empty() && all_lines.back().empty()) {
    all_lines.pop_back();
  }

  size_t start_idx = all_lines.size() > n ? all_lines.size() - n : 0;
  all_lines.erase(all_lines.begin(), all_lines.begin() + start_idx);

  return all_lines;
}

}  // namespace

LogMonitorDisplay::LogMonitorDisplay(size_t width)
    : width_(width), total_lines_drawn_(0) {}

void LogMonitorDisplay::DrawFile(SharedFD fd, const std::string& title) {
  std::vector<std::string> lines;
  if (fd->IsOpen()) {
    Result<std::vector<std::string>> lines_result = GetLastNLines(fd, 10);
    if (lines_result.ok()) {
      lines = *lines_result;
    } else {
      lines.push_back(absl::StrCat("Failed to read ", title, ":"));
      std::string error_str =
          lines_result.error().FormatForEnv(/*color=*/false);
      for (const auto& el : absl::StrSplit(error_str, '\n')) {
        if (!el.empty()) {
          lines.push_back(std::string(el));
        }
      }
    }
  } else {
    lines.push_back(absl::StrCat("Failed to read ", title, ": File not open"));
    lines.push_back(absl::StrCat("Error: ", fd->StrError()));
  }

  while (lines.size() < 10) {
    lines.push_back("");
  }

  DrawBorderedText(lines, title);
}

std::string LogMonitorDisplay::Finalize() {
  CHECK_GE(width_, 2);
  ss_ << "+" << std::string(width_ - 2, '-') << "+\n";
  total_lines_drawn_++;
  return ss_.str();
}

int LogMonitorDisplay::TotalLinesDrawn() const { return total_lines_drawn_; }

void LogMonitorDisplay::DrawBorderedText(const std::vector<std::string>& lines,
                                         const std::string& title) {
  std::string top_border = absl::StrCat("+--", title, " ");
  CHECK_GE(width_, 2);
  top_border.resize(width_ - 1, '-');
  ss_ << top_border << "+\n";

  for (std::string line : lines) {
    absl::StrReplaceAll({{"\t", "    "}, {"\r", ""}, {"\n", ""}}, &line);

    line.resize(width_ - 2, ' ');
    ss_ << "|" << line << "|\n";
  }
  total_lines_drawn_ += 1 + lines.size();
}

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
    std::cout << "\033[" << n << "A\033[J" << std::flush;
  }
}

class CvdMonitorCommandHandler : public CvdCommandHandler {
 public:
  CvdMonitorCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager} {}

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    CF_EXPECT(isatty(0),
              "The monitor command requires an interactive terminal.");

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

  cvd_common::Args CmdList() const override { return {kMonitorCmd}; }

  std::string SummaryHelp() const override { return kSummaryHelpText; }

  bool RequiresDeviceExists() const override { return true; }

  Result<std::string> DetailedHelp(
      const CommandRequest& request) const override {
    return kDetailedHelpText;
  }

 private:
  InstanceManager& instance_manager_;
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdMonitorCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdMonitorCommandHandler(instance_manager));
}

}  // namespace cuttlefish
