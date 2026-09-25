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
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "gflags/gflags.h"

#include "cuttlefish/host/commands/assemble_cvd/flags/from_gflags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/result/result.h"

DEFINE_string(use_vendor_boot_debug,
              CF_DEFAULTS_USE_VENDOR_BOOT_DEBUG ? "true" : "false",
              "Use vendor_boot-debug.img in the system_image_dir instead of "
              "vendor_boot.img. Has no effect if -vendor_boot_image is set.");

DEFINE_string(
    vendor_boot_image, CF_DEFAULTS_VENDOR_BOOT_IMAGE,
    "Location of cuttlefish vendor boot image. If empty, the image is "
    "selected from the directory specified by -system_image-dir based on "
    "the value of -use_vendor_boot_debug.");

namespace cuttlefish {

Result<VendorBootImageFlag> VendorBootImageFlag::FromGlobalGflags(
    const SystemImageDirFlag& system_image_dir) {
  gflags::CommandLineFlagInfo vendor_boot_image_flag_info =
      gflags::GetCommandLineFlagInfoOrDie("vendor_boot_image");
  gflags::CommandLineFlagInfo use_vendor_boot_debug_flag_info =
      gflags::GetCommandLineFlagInfoOrDie("use_vendor_boot_debug");

  FromGflags<bool> use_vendor_boot_debug_flag = CF_EXPECT(
      BoolFromGlobalGflags(use_vendor_boot_debug_flag_info, "vendor_boot_image",
                           CF_DEFAULTS_USE_VENDOR_BOOT_DEBUG));

  std::vector<std::string> vendor_boot_images =
      vendor_boot_image_flag_info.is_default
          ? std::vector<std::string>{}
          : absl::StrSplit(FLAGS_vendor_boot_image, ',');

  return VendorBootImageFlag(system_image_dir, vendor_boot_images,
                             use_vendor_boot_debug_flag.values);
}

std::string VendorBootImageFlag::VendorBootImageForIndex(size_t index) const {
  if (vendor_boot_images_.empty()) {
    std::string dir = system_image_dir_.ForIndex(index);
    bool use_vendor_boot_debug = false;

    if (index < use_vendor_boot_debugs_.size()) {
      use_vendor_boot_debug = use_vendor_boot_debugs_[index];
    } else if (!use_vendor_boot_debugs_.empty()) {
      use_vendor_boot_debug = use_vendor_boot_debugs_[0];
    }

    if (use_vendor_boot_debug) {
      dir += "/vendor_boot-debug.img";
    } else {
      dir += "/vendor_boot.img";
    }

    return dir;
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
    std::vector<std::string> vendor_boot_images,
    std::vector<bool> use_vendor_boot_debugs)
    : system_image_dir_(system_image_dir),
      vendor_boot_images_(std::move(vendor_boot_images)),
      use_vendor_boot_debugs_(std::move(use_vendor_boot_debugs)) {}

}  // namespace cuttlefish
