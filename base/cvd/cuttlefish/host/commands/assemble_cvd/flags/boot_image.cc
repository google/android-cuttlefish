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
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"

#include <stddef.h>

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(boot_image, CF_DEFAULTS_BOOT_IMAGE,
              "Location of cuttlefish boot image. If empty it is assumed to be "
              "boot.img in the directory specified by -system_image_dir.");

namespace cuttlefish {

BootImageFlag BootImageFlag::FromGlobalGflags(
    const SystemImageDirFlag& system_image_dir) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("boot_image");

  std::vector<std::string> boot_images =
      flag_info.is_default ? std::vector<std::string>{}
                           : android::base::Split(FLAGS_boot_image, ",");

  return BootImageFlag(system_image_dir, boot_images);
}

std::string BootImageFlag::BootImageForIndex(size_t index) const {
  if (boot_images_.empty()) {
    return system_image_dir_.ForIndex(index) + "/boot.img";
  } else if (index < boot_images_.size()) {
    return boot_images_[index];
  } else {
    return boot_images_[0];
  }
}

bool BootImageFlag::IsDefault() const { return boot_images_.empty(); }

BootImageFlag::BootImageFlag(const SystemImageDirFlag& system_image_dir,
                             std::vector<std::string> boot_images)
    : system_image_dir_(system_image_dir),
      boot_images_(std::move(boot_images)) {}

}  // namespace cuttlefish
