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
#include "cuttlefish/host/commands/assemble_cvd/flags/super_image.h"

#include <stddef.h>

#include <string>
#include <vector>

#include "absl/strings/str_split.h"
#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(
    super_image, CF_DEFAULTS_SUPER_IMAGE,
    "Location of cuttlefish super image. If empty it is assumed to be "
    "super.img in the directory specified by -system_image_dir.");

namespace cuttlefish {

SuperImageFlag SuperImageFlag::FromGlobalGflags(
    const SystemImageDirFlag& system_image_dir) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("super_image");

  std::vector<std::string> super_images =
      flag_info.is_default ? std::vector<std::string>{}
                           : absl::StrSplit(FLAGS_super_image, ',');

  return SuperImageFlag(system_image_dir, super_images);
}

std::string SuperImageFlag::SuperImageForIndex(size_t index) const {
  if (super_images_.empty()) {
    return system_image_dir_.ForIndex(index) + "/super.img";
  } else if (index < super_images_.size()) {
    return super_images_[index];
  } else {
    return super_images_[0];
  }
}

bool SuperImageFlag::IsDefault() const { return super_images_.empty(); }

SuperImageFlag::SuperImageFlag(const SystemImageDirFlag& system_image_dir,
                               std::vector<std::string> super_images)
    : system_image_dir_(system_image_dir),
      super_images_(std::move(super_images)) {}

}  // namespace cuttlefish
