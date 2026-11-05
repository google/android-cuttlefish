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

#include "cuttlefish/metrics/guest/boot_failure.h"

#include <stddef.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/directory_contents.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/log_names/log_names.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kUnexpectedExitMarker =
    "Detected unexpected exit of monitored subprocess ";

std::optional<std::string> ReadOptionalLogFile(
    std::string_view primary_path, std::string_view fallback_path = "") {
  if (!primary_path.empty() && FileExists(std::string(primary_path))) {
    const Result<std::string> contents =
        ReadFileContents(std::string(primary_path));
    if (contents.has_value()) {
      return *contents;
    }
  }
  if (!fallback_path.empty() && FileExists(std::string(fallback_path))) {
    const Result<std::string> contents =
        ReadFileContents(std::string(fallback_path));
    if (contents.has_value()) {
      return *contents;
    }
  }
  return std::nullopt;
}

bool HasTombstoneFiles(std::string_view instance_dir) {
  if (instance_dir.empty()) {
    return false;
  }
  const std::string tombstones_dir = absl::StrCat(instance_dir, "/tombstones");
  if (!FileExists(tombstones_dir)) {
    return false;
  }
  const Result<std::vector<std::string>> entries =
      DirectoryContents(tombstones_dir);
  if (!entries.has_value()) {
    return false;
  }
  for (const std::string& entry : *entries) {
    if (absl::StartsWith(entry, "tombstone_")) {
      return true;
    }
  }
  return false;
}

bool IsLauncherShutdownLine(std::string_view line) {
  return absl::StrContains(
             line,
             "TimeoutThreadLoop: Did not receive boot completion event") ||
         absl::StrContains(line, "Stopping monitored subprocesses") ||
         absl::StrContains(line, "Requesting stop") ||
         absl::StrContains(line, "Foreground launcher closed the pipe");
}

bool IsRunCvdStartedLine(std::string_view line) {
  return absl::StartsWith(line, "run_cvd") ||
         absl::StartsWith(line, "process_restarter") ||
         absl::StartsWith(line, "kernel_log_monitor") ||
         absl::StartsWith(line, "crosvm") ||
         absl::StartsWith(line, "log_tee") ||
         absl::StrContains(line, "Starting monitored subprocess") ||
         absl::StrContains(line, "Monitoring subprocesses");
}

}  // namespace

BootFailureCategory CategorizeBootFailure(const BootFailureLogs& logs) {
  if (!logs.assemble_log.has_value() && !logs.launcher_log.has_value() &&
      !logs.kernel_log.has_value() && !logs.logcat.has_value() &&
      !logs.has_tombstones) {
    return BootFailureCategory::kUnspecified;
  }

  bool run_cvd_started = false;
  bool vmm_failure = false;
  bool host_subprocess_crash = false;
  bool uboot_started = false;
  bool kernel_started = false;
  bool kernel_panic = false;
  bool init_started = false;
  bool userspace_crash = logs.has_tombstones;
  bool boot_started = false;
  bool boot_completed = false;

  if (logs.launcher_log.has_value()) {
    bool shutdown_started = false;
    for (const std::string_view raw_line :
         absl::StrSplit(*logs.launcher_log, '\n')) {
      const std::string_view line = absl::StripSuffix(raw_line, "\r");
      if (IsRunCvdStartedLine(line)) {
        run_cvd_started = true;
      }
      if (IsLauncherShutdownLine(line)) {
        shutdown_started = true;
      }
      if (!shutdown_started) {
        if (absl::StrContains(line, "ERROR crosvm] exiting with error") ||
            (absl::StrContains(line, "crosvm exited with exit code:") &&
             !absl::StrContains(line, "crosvm exited with exit code: 0"))) {
          vmm_failure = true;
        }
        const size_t marker_pos = line.find(kUnexpectedExitMarker);
        if (marker_pos != std::string_view::npos) {
          const std::string_view proc_path = absl::StripAsciiWhitespace(
              line.substr(marker_pos + kUnexpectedExitMarker.size()));
          const size_t slash_pos = proc_path.rfind('/');
          const std::string_view proc_name =
              slash_pos == std::string_view::npos
                  ? proc_path
                  : proc_path.substr(slash_pos + 1);
          if (proc_name == "process_restarter" || proc_name == "crosvm" ||
              absl::StartsWith(proc_name, "qemu")) {
            vmm_failure = true;
          } else if (!proc_name.empty() && proc_name != "metrics") {
            host_subprocess_crash = true;
          }
        }
        if (absl::StrContains(
                line,
                "Stopping all monitored processes due to unexpected exit of "
                "critical process") &&
            !vmm_failure) {
          host_subprocess_crash = true;
        }
      }
      if (absl::StrContains(line, "tombstone_receiver") &&
          absl::StrContains(line, "/tombstones/tombstone_")) {
        userspace_crash = true;
      }
      if (absl::StrContains(line, "GUEST_UBOOT_VERSION")) {
        uboot_started = true;
      }
      if (absl::StrContains(line, "GUEST_KERNEL_VERSION")) {
        kernel_started = true;
      }
      if (absl::StrContains(line, "GUEST_BUILD_FINGERPRINT") ||
          absl::StrContains(line,
                            "VIRTUAL_DEVICE_DISPLAY_POWER_MODE_CHANGED")) {
        init_started = true;
      }
      if (absl::StrContains(line, "VIRTUAL_DEVICE_BOOT_STARTED")) {
        boot_started = true;
      }
      if (absl::StrContains(line, "VIRTUAL_DEVICE_BOOT_COMPLETED") ||
          absl::StrContains(line, "Virtual device booted successfully")) {
        boot_completed = true;
      }
    }
  }

  if (logs.kernel_log.has_value()) {
    for (const std::string_view raw_line :
         absl::StrSplit(*logs.kernel_log, '\n')) {
      const std::string_view line = absl::StripSuffix(raw_line, "\r");
      if (absl::StrContains(line, "U-Boot ") ||
          absl::StrContains(line, "ANDROID: Loading ")) {
        uboot_started = true;
      }
      if (absl::StrContains(line, "Linux version ")) {
        kernel_started = true;
      }
      if (absl::StrContains(line, "Kernel panic - not syncing:") ||
          absl::StrContains(line, "Internal error: Oops") ||
          absl::StrContains(line, "BUG: unable to handle kernel")) {
        kernel_panic = true;
      }
      if (absl::StrContains(line, "init: ") ||
          absl::StrContains(line, "Run /init as init process")) {
        init_started = true;
      }
      if (absl::StrContains(line, "sys.init.updatable_crashing=1") ||
          absl::StrContains(line, "UPDATABLE_CRASHING") ||
          (absl::StrContains(line, "init: process with updatable components") &&
           absl::StrContains(line, "exited"))) {
        userspace_crash = true;
      }
      if (absl::StrContains(line, "VIRTUAL_DEVICE_BOOT_STARTED")) {
        boot_started = true;
      }
      if (absl::StrContains(line, "VIRTUAL_DEVICE_BOOT_COMPLETED")) {
        boot_completed = true;
      }
    }
  }

  if (logs.logcat.has_value()) {
    for (const std::string_view raw_line : absl::StrSplit(*logs.logcat, '\n')) {
      const std::string_view line = absl::StripSuffix(raw_line, "\r");
      if (!absl::StripAsciiWhitespace(line).empty()) {
        init_started = true;
      }
      if (absl::StrContains(line, "F DEBUG") ||
          absl::StrContains(line, "Fatal signal ") ||
          absl::StrContains(line, "FATAL EXCEPTION IN SYSTEM PROCESS") ||
          absl::StrContains(line, "F libc")) {
        userspace_crash = true;
      }
    }
  }

  if (boot_completed) {
    return BootFailureCategory::kUnspecified;
  }
  if (vmm_failure) {
    return BootFailureCategory::kVmmFailure;
  }
  if (host_subprocess_crash) {
    return BootFailureCategory::kHostSubprocessCrash;
  }
  if (!run_cvd_started && !uboot_started && !kernel_started && !init_started &&
      !boot_started && !userspace_crash) {
    return BootFailureCategory::kAssemblyFailure;
  }
  if (kernel_panic) {
    return BootFailureCategory::kKernelPanic;
  }
  if (!kernel_started && !init_started && !boot_started && !userspace_crash) {
    if (uboot_started) {
      return BootFailureCategory::kBootloaderFailure;
    }
    return BootFailureCategory::kPreBootloaderFailure;
  }
  if (userspace_crash) {
    return BootFailureCategory::kUserspaceCrash;
  }
  if (boot_started) {
    return BootFailureCategory::kBootTimeoutPostBootStarted;
  }
  if (init_started) {
    return BootFailureCategory::kBootTimeoutPreBootStarted;
  }
  return BootFailureCategory::kKernelHang;
}

BootFailureCategory CategorizeBootFailureFromDirs(
    std::string_view assembly_dir, std::string_view instance_dir) {
  const std::string assemble_path =
      assembly_dir.empty()
          ? ""
          : absl::StrCat(assembly_dir, "/", kLogNameAssembleCvd);
  const std::string launcher_logs_path =
      instance_dir.empty()
          ? ""
          : absl::StrCat(instance_dir, "/logs/", kLogNameLauncher);
  const std::string launcher_fallback_path =
      instance_dir.empty() ? ""
                           : absl::StrCat(instance_dir, "/", kLogNameLauncher);
  const std::string kernel_logs_path =
      instance_dir.empty()
          ? ""
          : absl::StrCat(instance_dir, "/logs/", kLogNameKernel);
  const std::string kernel_fallback_path =
      instance_dir.empty() ? ""
                           : absl::StrCat(instance_dir, "/", kLogNameKernel);
  const std::string logcat_logs_path =
      instance_dir.empty()
          ? ""
          : absl::StrCat(instance_dir, "/logs/", kLogNameLogcat);
  const std::string logcat_fallback_path =
      instance_dir.empty() ? ""
                           : absl::StrCat(instance_dir, "/", kLogNameLogcat);

  const BootFailureLogs logs{
      .assemble_log = ReadOptionalLogFile(assemble_path),
      .launcher_log =
          ReadOptionalLogFile(launcher_logs_path, launcher_fallback_path),
      .kernel_log = ReadOptionalLogFile(kernel_logs_path, kernel_fallback_path),
      .logcat = ReadOptionalLogFile(logcat_logs_path, logcat_fallback_path),
      .has_tombstones = HasTombstoneFiles(instance_dir),
  };
  return CategorizeBootFailure(logs);
}

}  // namespace cuttlefish
