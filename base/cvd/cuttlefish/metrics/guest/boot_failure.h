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

#include <optional>
#include <string>
#include <string_view>

namespace cuttlefish {

enum class BootFailureCategory {
  kUnspecified,
  kAssemblyFailure,
  kVmmFailure,
  kHostSubprocessCrash,
  kPreBootloaderFailure,
  kBootloaderFailure,
  kKernelPanic,
  kKernelHang,
  kUserspaceCrash,
  kBootTimeoutPreBootStarted,
  kBootTimeoutPostBootStarted,
};

struct BootFailureLogs {
  std::optional<std::string> assemble_log;
  std::optional<std::string> launcher_log;
  std::optional<std::string> kernel_log;
  std::optional<std::string> logcat;
  bool has_tombstones = false;
};

BootFailureCategory CategorizeBootFailure(const BootFailureLogs& logs);

BootFailureCategory CategorizeBootFailureFromDirs(
    std::string_view assembly_dir, std::string_view instance_dir);

}  // namespace cuttlefish
