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

#include "absl/strings/str_split.h"
#include "gflags/gflags.h"

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_builds.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(boot_image, CF_DEFAULTS_BOOT_IMAGE,
              "Location of cuttlefish boot image. If empty it is assumed to be "
              "boot.img in the directory specified by -system_image_dir.");

namespace cuttlefish {
namespace {

static constexpr std::string_view kName = "boot";

Result<std::vector<std::string>> Defaults(AndroidBuilds& android_builds) {
  std::vector<std::string> defaults;
  for (size_t i = 0; i < android_builds.Size(); i++) {
    AndroidBuild& build = android_builds.ForIndex(i);
    defaults.emplace_back(CF_EXPECT(build.ImageFile(kName)));
  }
  return defaults;
}

}  // namespace

Result<BootImageFlag> BootImageFlag::FromGlobalGflags(
    AndroidBuilds& android_builds) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("boot_image");

  return flag_info.is_default
             ? BootImageFlag(CF_EXPECT(Defaults(android_builds)), true)
             : BootImageFlag(absl::StrSplit(FLAGS_boot_image, ","), false);
}

BootImageFlag::BootImageFlag(std::vector<std::string> flag_values,
                             bool is_default)
    : FlagBase(std::move(flag_values), is_default) {}

}  // namespace cuttlefish
