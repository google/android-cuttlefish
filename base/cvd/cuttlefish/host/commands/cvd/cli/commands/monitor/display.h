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

#pragma once

#include <stddef.h>

#include <sstream>
#include <string>

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor_source.h"

namespace cuttlefish {

/**
 * The result of finalizing the log monitor display.
 */
struct LogMonitorDisplayResult {
  /** The complete formatted output string with all boxes and borders. */
  std::string output;
  /** The total number of lines in the output, including all borders. */
  int total_lines_drawn;
};

/**
 * LogMonitorDisplay formats and accumulates multiple log streams into a
 * structured, bordered terminal display. It supports drawing boxes around
 * log content, truncating long lines while preserving ANSI colors, and
 * optionally stripping colors if the output terminal doesn't support them.
 *
 * Example usage:
 * @code
 *   LogMonitorDisplay display(80); // 80 columns wide
 *   display.DrawReport(launcher_source, 20);
 *   display.DrawReport(kernel_source, 40);
 *   LogMonitorDisplayResult result = display.Finalize();
 *   std::cout << result.output << std::flush;
 * @endcode
 */
class LogMonitorDisplay {
 public:
  /**
   * Initializes the display with a fixed character width.
   */
  LogMonitorDisplay(size_t width);

  /* Draw the output of a single monitor source. */
  void DrawReport(MonitorSource*, size_t lines);
  void DrawReport(MonitorOutput, size_t lines);

  /**
   * Appends the final bottom border to the display and returns the
   * complete accumulated string and total line count.
   */
  LogMonitorDisplayResult Finalize();

 private:
  /**
   * Helper to draw a list of text lines inside a bordered box with a title.
   */
  void DrawBorderedText(const MonitorOutput&);

  /**
   * Formats a single line of text: parses it for specific log types (like
   * logcat, kernel), applies color formatting, truncates to fit the display
   * width, pads with spaces, and adds borders.
   */
  void FormatAndDrawLine(std::string_view formatted);

  size_t width_;
  std::stringstream ss_;
  int total_lines_drawn_;
  bool colorize_;
};

}  // namespace cuttlefish
