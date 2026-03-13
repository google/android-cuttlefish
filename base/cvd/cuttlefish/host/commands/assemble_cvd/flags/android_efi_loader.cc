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
#include "cuttlefish/host/commands/assemble_cvd/flags/android_efi_loader.h"

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "gflags/gflags.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vm_manager.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"

DEFINE_string(android_efi_loader, CF_DEFAULTS_ANDROID_EFI_LOADER,
              "Location of android EFI loader for android efi load flow.");

namespace cuttlefish {
namespace {

static constexpr std::string_view kName = "/android_efi_loader.efi";

std::vector<std::string> DefaultPaths(
    const SystemImageDirFlag& system_image_dirs) {
  std::vector<std::string> paths;
  for (std::string_view system_image_dir : system_image_dirs.AsVector()) {
    // EFI loader isn't present in the output folder by default and can be only
    // fetched by --android_efi_loader_build in fetch_cvd, so pick it only in
    // case it's present.
    std::string path = absl::StrCat(system_image_dir, kName);
    paths.emplace_back(FileExists(path) ? std::move(path) : "");
  }
  return paths;
}

}  // namespace

/* `--android_efi_loader` flag */
AndroidEfiLoaderFlag AndroidEfiLoaderFlag::FromGlobalGflags(
    const SystemImageDirFlag& system_image_dir, const VmManagerFlag& vmm) {
  const gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("android_efi_loader");
  if (!VmManagerIsCrosvm(vmm) && !VmManagerIsQemu(vmm)) {
    return AndroidEfiLoaderFlag({}, flag_info.is_default);
  }
  if (flag_info.is_default) {
    return AndroidEfiLoaderFlag(DefaultPaths(system_image_dir), true);
  } else {
    return AndroidEfiLoaderFlag(absl::StrSplit(FLAGS_android_efi_loader, ","),
                                false);
  }
}

AndroidEfiLoaderFlag::AndroidEfiLoaderFlag(std::vector<std::string> flag_values,
                                           bool is_default)
    : FlagBase(std::move(flag_values), is_default) {}

}  // namespace cuttlefish
