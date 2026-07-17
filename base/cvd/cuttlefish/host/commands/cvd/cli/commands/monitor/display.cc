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

#include <stddef.h>
#include <stdio.h>

#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "android-base/file.h"

#include "cuttlefish/ansi_codes/should_color.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor_source.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/truncate.h"

namespace cuttlefish {

LogMonitorDisplay::LogMonitorDisplay(size_t width)
    : width_(width), total_lines_drawn_(0), colorize_(ShouldColorStdout()) {}

void LogMonitorDisplay::DrawReport(MonitorSource* source, size_t lines) {
  MonitorOutput output = source ? source->Report(lines, width_ - 2)
                                : MonitorOutput("No data", {"No data"});
  DrawReport(std::move(output), lines);
}

void LogMonitorDisplay::DrawReport(MonitorOutput output, size_t lines) {
  if (output.lines.size() > lines) {
    size_t to_erase = output.lines.size() - lines;
    output.lines.erase(output.lines.begin(), output.lines.begin() + to_erase);
  } else {
    output.lines.resize(lines, "");
  }
  DrawBorderedText(output);
}

LogMonitorDisplayResult LogMonitorDisplay::Finalize() {
  CHECK_GE(width_, 2);
  ss_ << "+" << std::string(width_ - 2, '-') << "+\n";
  total_lines_drawn_++;
  return LogMonitorDisplayResult{
      .output = ss_.str(),
      .total_lines_drawn = total_lines_drawn_,
  };
}

void LogMonitorDisplay::DrawBorderedText(const MonitorOutput& output) {
  const std::string title = android::base::Basename(output.title);
  std::string top_border = absl::StrCat("+--", title, " ");
  CHECK_GE(width_, 2);
  top_border.resize(width_ - 1, '-');
  ss_ << top_border << "+\n";

  for (const std::string& raw_line : output.lines) {
    std::string sanitized_holder;
    std::string_view line = raw_line;
    if (line.find_first_of("\t\r\n") != std::string_view::npos) {
      sanitized_holder = raw_line;
      absl::StrReplaceAll({{"\t", "    "}, {"\r", ""}, {"\n", ""}},
                          &sanitized_holder);
      line = sanitized_holder;
    }
    FormatAndDrawLine(line);
  }
  total_lines_drawn_ += 1 + output.lines.size();
}

void LogMonitorDisplay::FormatAndDrawLine(std::string_view formatted) {
  auto [final_line, visible_len] = TruncateColoredString(formatted, width_ - 2);
  if (visible_len < width_ - 2) {
    absl::StrAppend(&final_line, std::string(width_ - 2 - visible_len, ' '));
  }
  if (!colorize_) {
    final_line = StripColorCodes(final_line);
  }
  ss_ << "|" << final_line << "|\n";
}

}  // namespace cuttlefish
