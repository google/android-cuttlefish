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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/display.h"

#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/kernel.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/launcher.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/log_tee.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/logcat.h"
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
    : width_(width), total_lines_drawn_(0), colorize_(isatty(1)) {}

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

  for (const std::string& raw_line : lines) {
    std::string sanitized_holder;
    std::string_view line = raw_line;
    if (line.find_first_of("\t\r\n") != std::string_view::npos) {
      sanitized_holder = raw_line;
      absl::StrReplaceAll({{"\t", "    "}, {"\r", ""}, {"\n", ""}},
                          &sanitized_holder);
      line = sanitized_holder;
    }

    if (title == kLogNameLauncher && line.find("log_tee(") == 0) {
      // We intentionally mutate `line` in-place here. Even if `ParseLogTeeLine`
      // fails (e.g., for plain unstructured subprocess output), we want to drop
      // the long `log_tee(...) ... ]` prefix for subsequent fallback
      // formatting.
      const size_t bracket = line.find(']');
      if (bracket != std::string_view::npos) {
        line.remove_prefix(bracket + 1);
        const size_t first_non_space = line.find_first_not_of(" \t");
        if (first_non_space != std::string_view::npos) {
          line.remove_prefix(first_non_space);
        } else {
          line = "";
        }
      }

      if (Result<LogTeeLine> parsed = ParseLogTeeLine(line); parsed.ok()) {
        ss_ << "|" << FormatLogTeeLine(*parsed, colorize_, width_ - 2) << "|\n";
        continue;
      }
    }

    if (title == kLogNameLogcat) {
      if (Result<LogcatLine> parsed = ParseLogcatLine(line); parsed.ok()) {
        ss_ << "|" << FormatLogcatLine(*parsed, colorize_, width_ - 2) << "|\n";
        continue;
      }
    }

    if (title == kLogNameKernel) {
      if (Result<KernelLine> parsed = ParseKernelLine(line); parsed.ok()) {
        ss_ << "|" << FormatKernelLine(*parsed, colorize_, width_ - 2) << "|\n";
        continue;
      }
    }

    if (title == kLogNameLauncher) {
      if (Result<LauncherLine> parsed = ParseLauncherLine(line); parsed.ok()) {
        ss_ << "|" << FormatLauncherLine(*parsed, colorize_, width_ - 2)
            << "|\n";
        continue;
      }
    }

    std::string fallback_line(line);
    fallback_line.resize(width_ - 2, ' ');
    ss_ << "|" << fallback_line << "|\n";
  }
  total_lines_drawn_ += 1 + lines.size();
}

}  // namespace cuttlefish
