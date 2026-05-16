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

class LogMonitorDisplay {
 public:
  LogMonitorDisplay(size_t width);

  void DrawFile(SharedFD fd, const std::string& title);

  std::string Finalize();

  int TotalLinesDrawn() const;

 private:
  void DrawBorderedText(const std::vector<std::string>& lines,
                        const std::string& title);

  size_t width_;
  std::stringstream ss_;
  int total_lines_drawn_;
  bool colorize_;
};

}  // namespace cuttlefish
