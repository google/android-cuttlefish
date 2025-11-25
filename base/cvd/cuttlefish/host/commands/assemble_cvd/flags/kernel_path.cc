/*
 * Copyright (C) 2019 The Android Open Source Project
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
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"

DEFINE_string(kernel_path, CF_DEFAULTS_KERNEL_PATH,
              "Path to the kernel. Overrides the one from the boot image");

namespace cuttlefish {

KernelPathFlag KernelPathFlag::FromGlobalGflags(
    const FetcherConfigs& fetcher_configs) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("kernel_path");

  std::vector<std::string> kernel_paths;

  if (flag_info.is_default) {
    for (size_t i = 0; i < fetcher_configs.Size(); ++i) {
      const FetcherConfig& fetcher_config = fetcher_configs.ForInstance(i);
      kernel_paths.emplace_back(fetcher_config.FindCvdFileWithSuffix(
          FileSource::KERNEL_BUILD, "kernel"));
    }
  } else {
    kernel_paths = android::base::Split(flag_info.current_value, ",");
  }

  return KernelPathFlag(std::move(kernel_paths));
}

std::string KernelPathFlag::KernelPathForIndex(size_t index) const {
  if (kernel_paths_.empty()) {
    return "";
  } else if (index >= kernel_paths_.size()) {
    return kernel_paths_[0];
  } else {
    return kernel_paths_[index];
  }
}

bool KernelPathFlag::HasValue() const {
  auto is_set = [](const std::string& str) { return !str.empty(); };
  return std::any_of(kernel_paths_.begin(), kernel_paths_.end(), is_set);
}

KernelPathFlag::KernelPathFlag(std::vector<std::string> kernel_paths)
    : kernel_paths_(std::move(kernel_paths)) {}

}  // namespace cuttlefish
