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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "gflags/gflags.h"

#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(boot_image, CF_DEFAULTS_BOOT_IMAGE,
              "Location of cuttlefish boot image. If empty it is assumed to be "
              "boot.img in the directory specified by -system_image_dir.");

namespace cuttlefish {
namespace {

static constexpr std::string_view kName = "boot.img";

std::vector<std::string> Defaults(const SystemImageDirFlag& system_image_dirs) {
  std::vector<std::string> defaults;
  for (std::string_view system_image_dir : system_image_dirs.AsVector()) {
    defaults.emplace_back(absl::StrCat(system_image_dir, "/", kName));
  }
  return defaults;
}

}  // namespace

BootImageFlag BootImageFlag::FromGlobalGflags(
    const SystemImageDirFlag& system_image_dir) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("boot_image");

  return flag_info.is_default
             ? BootImageFlag(Defaults(system_image_dir), true)
             : BootImageFlag(absl::StrSplit(FLAGS_boot_image, ","), false);
}

bool BootImageFlag::IsDefault() const { return is_default_; }

BootImageFlag::BootImageFlag(std::vector<std::string> paths, bool is_default)
    : FlagBase(std::move(paths)), is_default_(is_default) {}

}  // namespace cuttlefish
