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
#include "cuttlefish/host/commands/assemble_cvd/flags/bootloader.h"

#include <stddef.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "fmt/format.h"
#include "gflags/gflags.h"

#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vm_manager.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"
#include "cuttlefish/result/result.h"

DEFINE_string(bootloader, CF_DEFAULTS_BOOTLOADER, "Bootloader binary path");

namespace cuttlefish {
namespace {

constexpr std::string_view ArchDirName(Arch arch) {
  switch (arch) {
    case Arch::Arm64:
      return "aarch64";
    case Arch::Arm:
      return "arm";
    case Arch::RiscV64:
      return "riscv64";
    case Arch::X86:
    case Arch::X86_64:
      return "x86_64";
  }
}

}  // namespace

/* Device bootloader flag, `--bootloader` */
Result<BootloaderFlag> BootloaderFlag::FromGlobalGflags(
    const std::vector<GuestConfig>& guest_configs,
    const SystemImageDirFlag& system_image_dir,
    const VmManagerFlag& vm_manager) {
  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("bootloader");
  if (!flag_info.is_default) {
    std::vector<std::string> bootloader_values =
        absl::StrSplit(FLAGS_bootloader, ',');
    std::vector<bool> is_default_values(bootloader_values.size(),
                                        flag_info.is_default);
    for (size_t i = 0; i < is_default_values.size(); i++) {
      if (bootloader_values[i].empty()) {
        is_default_values[i] = true;
      }
    }
    return BootloaderFlag(bootloader_values, is_default_values);
  }

  std::string_view vmm;
  switch (vm_manager.Mode()) {
    case VmmMode::kCrosvm:
      vmm = "crosvm";
      break;
    case VmmMode::kQemu:
      vmm = "qemu";
      break;
    default:
      return BootloaderFlag({}, {});
  }

  std::vector<std::string> bootloaders;
  for (size_t instance = 0; instance < guest_configs.size(); instance++) {
    // /bootloader isn't present in the output folder by default and can be only
    // fetched by `--bootloader` in fetch_cvd, so pick it only in case it is
    // present.
    std::string path = system_image_dir.ForIndex(instance) + "/bootloader";
    if (!FileExists(path)) {
      std::string_view arch = ArchDirName(guest_configs[instance].target_arch);
      std::string rel_path =
          fmt::format("etc/bootloader_{}/bootloader.{}", arch, vmm);
      path = DefaultHostArtifactsPath(rel_path);
      CF_EXPECT(FileExists(path));
    }
    bootloaders.emplace_back(std::move(path));
  }
  return BootloaderFlag(
      std::move(bootloaders),
      std::vector<bool>(bootloaders.size(), flag_info.is_default));
}

BootloaderFlag::BootloaderFlag(std::vector<std::string> bootloaders,
                               std::vector<bool> is_default_values)
    : bootloaders_(std::move(bootloaders)),
      is_default_values_(std::move(is_default_values)) {}

std::string BootloaderFlag::BootloaderForInstance(size_t instance_index) const {
  if (bootloaders_.empty()) {
    return "";
  } else if (instance_index < bootloaders_.size()) {
    return bootloaders_[instance_index];
  } else {
    return bootloaders_[0];
  }
}

bool BootloaderFlag::IsDefaultForIndex(size_t index) const {
  if (is_default_values_.empty()) {
    return bootloaders_.empty();
  } else if (index < is_default_values_.size()) {
    return is_default_values_[index];
  } else {
    return is_default_values_[0];
  }
}

}  // namespace cuttlefish
