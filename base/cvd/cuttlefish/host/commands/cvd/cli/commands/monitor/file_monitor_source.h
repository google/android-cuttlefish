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

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor_source.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class FileMonitorSource : public MonitorSource {
 public:
  FileMonitorSource(
      std::string name, std::unique_ptr<ReaderSeeker> file_io,
      std::function<Result<std::string>(std::string_view)> colorize_line);

  MonitorOutput Report(size_t rows, size_t columns) override;

 private:
  std::string name_;
  std::unique_ptr<ReaderSeeker> file_io_;
  std::function<Result<std::string>(std::string_view)> colorize_line_;
};

}  // namespace cuttlefish
