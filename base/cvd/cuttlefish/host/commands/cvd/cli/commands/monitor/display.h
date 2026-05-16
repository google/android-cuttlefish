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

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"

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
 *   display.DrawFile(launcher_fd, "launcher.log");
 *   display.DrawFile(kernel_fd, "kernel.log");
 *   auto result = display.Finalize();
 *   std::cout << result.output << std::flush;
 *
 *   // Use the returned line count to know how many
 *   // lines to clear for a redraw:
 *   ClearLastNLines(result.total_lines_drawn);
 * @endcode
 */
class LogMonitorDisplay {
 public:
  /**
   * Initializes the display with a fixed character width.
   */
  LogMonitorDisplay(size_t width);

  /**
   * Reads the recent content of the given file descriptor and draws it
   * within a bordered box titled with the provided name.
   * If the file cannot be read, an error box is drawn instead.
   */
  void DrawFile(SharedFD fd, const std::string& title);

  /**
   * Appends the final bottom border to the display and returns the
   * complete accumulated string and total line count.
   */
  LogMonitorDisplayResult Finalize();

 private:
  /**
   * Helper to draw a list of text lines inside a bordered box with a title.
   */
  void DrawBorderedText(const std::vector<std::string>& lines,
                        const std::string& title);

  /**
   * Formats a single line of text: parses it for specific log types (like
   * logcat, kernel), applies color formatting, truncates to fit the display
   * width, pads with spaces, and adds borders.
   */
  void FormatAndDrawLine(const std::string& formatted);

  size_t width_;
  std::stringstream ss_;
  int total_lines_drawn_;
  bool colorize_;
};

}  // namespace cuttlefish
