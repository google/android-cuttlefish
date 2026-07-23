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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/log_sources.h"

#include <fcntl.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/file_monitor_source.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/kernel.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/launcher.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/log_tee.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/logcat.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor_source.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/libs/log_names/log_names.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<std::string> ColorLauncherOrLogTee(std::string_view line) {
  if (line.find("log_tee(") != 0) {
    return CF_EXPECT(ColorLauncherLine(line));
  }
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
  return CF_EXPECT(ColorLogTeeLine(line));
}

}  // namespace

std::unique_ptr<MonitorSource> LauncherLogMonitorSource(
    const LocalInstance& instance) {
  const std::string launcher =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameLauncher);
  const std::string assemble =
      absl::StrCat(instance.AssemblyDirectory(), "/", kLogNameAssembleCvd);
  const std::string path = FileExists(launcher) ? launcher : assemble;
  if (!FileExists(path)) {
    return {};
  }
  SharedFD fd = SharedFD::Open(path, O_RDONLY);
  if (!fd->IsOpen() || fd->LSeek(0, SEEK_END) <= 0) {
    return {};
  }
  std::unique_ptr<ReaderSeeker> io = std::make_unique<SharedFdIo>(fd);
  return std::make_unique<FileMonitorSource>(path, std::move(io),
                                             ColorLauncherOrLogTee);
}

std::unique_ptr<MonitorSource> KernelLogMonitorSource(
    const LocalInstance& instance) {
  const std::string path =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameKernel);
  if (!FileExists(path)) {
    return {};
  }
  SharedFD fd = SharedFD::Open(path, O_RDONLY);
  if (!fd->IsOpen() || fd->LSeek(0, SEEK_END) <= 0) {
    return {};
  }
  std::unique_ptr<ReaderSeeker> io = std::make_unique<SharedFdIo>(fd);
  return std::make_unique<FileMonitorSource>(path, std::move(io),
                                             ColorKernelLine);
}

std::unique_ptr<MonitorSource> LogcatMonitorSource(
    const LocalInstance& instance) {
  const std::string path =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameLogcat);
  if (!FileExists(path)) {
    return {};
  }
  SharedFD fd = SharedFD::Open(path, O_RDONLY);
  if (!fd->IsOpen() || fd->LSeek(0, SEEK_END) <= 0) {
    return {};
  }
  std::unique_ptr<ReaderSeeker> io = std::make_unique<SharedFdIo>(fd);
  return std::make_unique<FileMonitorSource>(path, std::move(io),
                                             ColorLogcatLine);
}

}  // namespace cuttlefish
