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
#include "cuttlefish/host/commands/assemble_cvd/flags/initramfs_path.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"

DEFINE_string(initramfs_path, CF_DEFAULTS_INITRAMFS_PATH,
              "Path to the initramfs. Overrides the one from the boot image");

namespace cuttlefish {

InitramfsPathFlag InitramfsPathFlag::FromGlobalGflags(
    const FetcherConfigs& fetcher_configs) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("initramfs_path");

  std::vector<std::string> initramfs_paths;
  if (flag_info.is_default) {
    for (size_t i = 0; i < fetcher_configs.Size(); ++i) {
      const FetcherConfig& fetcher_config = fetcher_configs.ForInstance(i);
      initramfs_paths.emplace_back(fetcher_config.FindCvdFileWithSuffix(
          FileSource::KERNEL_BUILD, "initramfs.img"));
    }
  } else {
    initramfs_paths = android::base::Split(flag_info.current_value, ",");
  }

  return InitramfsPathFlag(std::move(initramfs_paths));
}

std::string InitramfsPathFlag::InitramfsPathForIndex(size_t index) const {
  if (initramfs_paths_.empty()) {
    return "";
  } else if (index >= initramfs_paths_.size()) {
    return initramfs_paths_[0];
  } else {
    return initramfs_paths_[index];
  }
}

bool InitramfsPathFlag::HasValue() const {
  auto is_set = [](const std::string& str) { return !str.empty(); };
  return std::any_of(initramfs_paths_.begin(), initramfs_paths_.end(), is_set);
}

InitramfsPathFlag::InitramfsPathFlag(std::vector<std::string> initramfs_paths)
    : initramfs_paths_(std::move(initramfs_paths)) {}

}  // namespace cuttlefish
