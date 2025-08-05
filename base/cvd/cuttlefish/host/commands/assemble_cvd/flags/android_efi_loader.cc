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

#include <gflags/gflags.h>
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vm_manager.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"

DEFINE_string(android_efi_loader, CF_DEFAULTS_ANDROID_EFI_LOADER,
              "Location of android EFI loader for android efi load flow.");

namespace cuttlefish {

/* `--android_efi_loader` flag */
AndroidEfiLoaderFlag AndroidEfiLoaderFlag::FromGlobalGflags(
    const SystemImageDirFlag& system_image_dir, const VmManagerFlag& vmm) {
  bool enabled = false;
  switch (vmm.Mode()) {
    case VmmMode::kCrosvm:
    case VmmMode::kQemu:
      enabled = true;
      break;
    case VmmMode::kGem5:
    case VmmMode::kUnknown:
      enabled = false;
      break;
  }
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("bootloader");
  if (flag_info.is_default) {
    return AndroidEfiLoaderFlag(system_image_dir, {}, enabled);
  }
  std::vector<std::string> loaders =
      absl::StrSplit(FLAGS_android_efi_loader, ",");
  return AndroidEfiLoaderFlag(system_image_dir, std::move(loaders), enabled);
}

AndroidEfiLoaderFlag::AndroidEfiLoaderFlag(
    const SystemImageDirFlag& system_image_dir, std::vector<std::string> flag,
    bool enabled)
    : system_image_dir_(system_image_dir),
      flag_(std::move(flag)),
      enabled_(enabled) {}

std::string AndroidEfiLoaderFlag::AndroidEfiLoaderForInstance(
    size_t instance_index) const {
  if (instance_index < flag_.size()) {
    return flag_[instance_index];
  } else if (!flag_.empty()) {
    return flag_[0];
  } else if (!enabled_) {
    return "";
  }
  // EFI loader isn't present in the output folder by default and can be only
  // fetched by --android_efi_loader_build in fetch_cvd, so pick it only in case
  // it's present.
  std::string downloaded =
      system_image_dir_.ForIndex(instance_index) + "/android_efi_loader.efi";
  if (FileExists(downloaded)) {
    return downloaded;
  }
  return "";
}

}  // namespace cuttlefish
