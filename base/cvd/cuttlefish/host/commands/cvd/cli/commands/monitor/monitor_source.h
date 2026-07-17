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

#include <string>
#include <vector>

namespace cuttlefish {

struct MonitorOutput {
  MonitorOutput();
  MonitorOutput(std::string title, std::vector<std::string> lines);

  std::string title;
  std::vector<std::string> lines;
};

class MonitorSource {
 public:
  virtual ~MonitorSource();
  /**
   * Pulls new information from the data source if there is any and returns the
   * text that should be shown in the monitor window.
   *
   * `rows` and `columns` are hints for the output. May output more or less.
   */
  virtual MonitorOutput Report(size_t rows, size_t columns) = 0;
};

}  // namespace cuttlefish
