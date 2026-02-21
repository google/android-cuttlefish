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
#include "cuttlefish/host/commands/assemble_cvd/flags/vendor_boot_image.h"

#include <stddef.h>

#include <string>
#include <vector>

#include "absl/strings/str_split.h"
#include <gflags/gflags.h>

#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(
    vendor_boot_image, CF_DEFAULTS_VENDOR_BOOT_IMAGE,
    "Location of cuttlefish vendor boot image. If empty it is assumed to "
    "be vendor_boot.img in the directory specified by -system_image_dir.");

namespace cuttlefish {

VendorBootImageFlag VendorBootImageFlag::FromGlobalGflags(
    const SystemImageDirFlag& system_image_dir) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("vendor_boot_image");

  std::vector<std::string> vendor_boot_images =
      flag_info.is_default ? std::vector<std::string>{}
                           : absl::StrSplit(FLAGS_vendor_boot_image, ',');

  return VendorBootImageFlag(system_image_dir, vendor_boot_images);
}

std::string VendorBootImageFlag::VendorBootImageForIndex(size_t index) const {
  if (vendor_boot_images_.empty()) {
    return system_image_dir_.ForIndex(index) + "/vendor_boot.img";
  } else if (index < vendor_boot_images_.size()) {
    return vendor_boot_images_[index];
  } else {
    return vendor_boot_images_[0];
  }
}

bool VendorBootImageFlag::IsDefault() const {
  return vendor_boot_images_.empty();
}

VendorBootImageFlag::VendorBootImageFlag(
    const SystemImageDirFlag& system_image_dir,
    std::vector<std::string> vendor_boot_images)
    : system_image_dir_(system_image_dir),
      vendor_boot_images_(std::move(vendor_boot_images)) {}

}  // namespace cuttlefish
