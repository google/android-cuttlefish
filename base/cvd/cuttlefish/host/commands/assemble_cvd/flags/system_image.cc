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
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image.h"

#include <stddef.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "gflags/gflags.h"

#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/fetcher_configs.h"
#include "cuttlefish/host/libs/config/file_source.h"

DEFINE_string(system_image, CF_DEFAULTS_SYSTEM_IMAGE,
              "Path to the system image. Overrides the one in the super image");

namespace cuttlefish {

SystemImageFlag SystemImageFlag::FromGlobalGflags(
    const FetcherConfigs& fetcher_configs) {
  const gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("system_image");

  std::vector<std::string> system_images;

  if (flag_info.is_default) {
    for (size_t i = 0; i < fetcher_configs.Size(); ++i) {
      const FetcherConfig& fetcher_config = fetcher_configs.ForInstance(i);
      system_images.emplace_back(fetcher_config.FindCvdFileWithSuffix(
          FileSource::SYSTEM_IMAGE_BUILD, "system.img"));
    }
  } else {
    system_images = absl::StrSplit(flag_info.current_value, ',');
  }

  return SystemImageFlag(std::move(system_images));
}

std::string SystemImageFlag::SystemImageForIndex(const size_t index) const {
  if (system_images_.empty()) {
    return "";
  } else if (index >= system_images_.size()) {
    return system_images_[0];
  } else {
    return system_images_[index];
  }
}

bool SystemImageFlag::HasValue() const {
  auto is_set = [](const std::string& str) { return !str.empty(); };
  return std::any_of(system_images_.begin(), system_images_.end(), is_set);
}

SystemImageFlag::SystemImageFlag(std::vector<std::string> system_images)
    : system_images_(std::move(system_images)) {}

}  // namespace cuttlefish
