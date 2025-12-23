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
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"

#include <string>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/result/result.h"

DEFINE_string(system_image_dir, CF_DEFAULTS_SYSTEM_IMAGE_DIR,
              "Directory where `.img` files are loaded from");

namespace cuttlefish {

Result<SystemImageDirFlag> SystemImageDirFlag::FromGlobalGflags() {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("system_image_dir");

  std::string system_image_dir_flag = flag_info.is_default
                                          ? DefaultGuestImagePath("")
                                          : flag_info.current_value;

  CF_EXPECT(!system_image_dir_flag.empty(),
            "--system_image_dir must be specified.");

  std::vector<std::string> paths =
      android::base::Split(system_image_dir_flag, ",");
  CF_EXPECT(!paths.empty());

  return SystemImageDirFlag(std::move(paths));
}

std::string SystemImageDirFlag::ForIndex(size_t argument_index) const {
  if (argument_index >= system_image_dirs_.size()) {
    argument_index = 0;
  }
  return system_image_dirs_[argument_index];
}

const std::vector<std::string>& SystemImageDirFlag::AsVector() const {
  return system_image_dirs_;
}

SystemImageDirFlag::SystemImageDirFlag(
    std::vector<std::string> system_image_dirs)
    : system_image_dirs_(std::move(system_image_dirs)) {}

}  // namespace cuttlefish
