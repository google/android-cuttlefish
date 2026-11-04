/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/metrics/guest/parsed_flags.h"

#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_builds.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/find_builds.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/bootloader.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/cpus.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/daemon.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/data_policy.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/extra_kernel_cmdline.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/gpu_context_types.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/gpu_mode.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/guest_enforce_security.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/initramfs_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/memory_mb.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/qemu_binary_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/restart_subprocesses.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/super_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vendor_boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vm_manager.h"
#include "cuttlefish/host/commands/assemble_cvd/graphics_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"
#include "cuttlefish/host/commands/assemble_cvd/resolve_instance_files.h"
#include "cuttlefish/host/graphics_detector/graphics_detector.pb.h"
#include "cuttlefish/host/libs/config/fetcher_configs.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<ParsedFlags> GetBaseParsedFlags() {
  return ParsedFlags{
      .cpus = CF_EXPECT(CpusFlag::FromGlobalGflags()),
      .daemon = CF_EXPECT(DaemonFlag::FromGlobalGflags()),
      .data_policy = CF_EXPECT(DataPolicyFlag::FromGlobalGflags()),
      .extra_kernel_cmdline = ExtraKernelCmdlineFlag::FromGlobalGflags(),
      .gpu_mode_requested = CF_EXPECT(GpuModeFlag::FromGlobalGflags()),
      .guest_enforce_security =
          CF_EXPECT(GuestEnforceSecurityFlag::FromGlobalGflags()),
      .memory_mb = CF_EXPECT(MemoryMbFlag::FromGlobalGflags()),
      .qemu_binary_dir = CF_EXPECT(QemuBinaryDirFlag::FromGlobalGflags()),
      .restart_subprocesses =
          CF_EXPECT(RestartSubprocessesFlag::FromGlobalGflags()),
      .system_image_dir = CF_EXPECT(SystemImageDirFlag::FromGlobalGflags()),
  };
}

Result<std::vector<GuestConfig>> GetGuestConfigs(
    const FetcherConfigs& fetcher_configs, const BootImageFlag& boot_image,
    const SuperImageFlag& super_image,
    const SystemImageDirFlag& system_image_dir,
    const VendorBootImageFlag& vendor_boot) {
  const InitramfsPathFlag initramfs_path =
      InitramfsPathFlag::FromGlobalGflags(fetcher_configs);
  const KernelPathFlag kernel_path =
      KernelPathFlag::FromGlobalGflags(fetcher_configs);
  CF_EXPECT(ResolveInstanceFiles(boot_image, initramfs_path, kernel_path,
                                 super_image, system_image_dir, vendor_boot),
            "Failed to resolve instance files");
  // Depends on ResolveInstanceFiles to set flag globals
  return CF_EXPECT(ReadGuestConfig(boot_image, kernel_path, system_image_dir));
}

Result<std::vector<GpuMode>> GetGpuModeUsed(
    const GpuModeFlag& gpu_mode_requested, const VmManagerFlag& vm_manager,
    const std::vector<GuestConfig>& guest_configs) {
  const gfxstream::proto::GraphicsAvailability graphics_availability =
      GetGraphicsAvailabilityWithSubprocessCheck();
  const GpuContextTypesFlag gpu_context_types =
      CF_EXPECT(GpuContextTypesFlag::FromGlobalGflags());

  std::vector<GpuMode> gpu_mode_used;
  for (int i = 0; i < gpu_mode_requested.Size(); i++) {
    gpu_mode_used.emplace_back(CF_EXPECT(SelectGpuMode(
        gpu_mode_requested.ForIndex(i), vm_manager.Mode(), guest_configs[i],
        gpu_context_types.ForIndex(i), graphics_availability)));
  }
  return gpu_mode_used;
}

}  // namespace

Result<ParsedFlags> GetParsedFlags() {
  ParsedFlags parsed_flags = CF_EXPECT(GetBaseParsedFlags());

  const FetcherConfigs fetcher_configs = FetcherConfigs::ReadFromDirectories(
      parsed_flags.system_image_dir.AsVector());
  // the artifacts might not yet exist for certain device events
  // for example, cvd create --config_file VM_INSTATIATION is pre-fetching
  Result<AndroidBuilds> android_builds_result =
      FindAndroidBuilds(parsed_flags.system_image_dir, fetcher_configs);
  if (android_builds_result) {
    Result<BootImageFlag> boot_image_result =
        BootImageFlag::FromGlobalGflags(*android_builds_result);
    if (boot_image_result) {
      parsed_flags.boot_image.emplace(*boot_image_result);
    }
    parsed_flags.super_image.emplace(
        SuperImageFlag::FromGlobalGflags(parsed_flags.system_image_dir));
    parsed_flags.vendor_boot.emplace(VendorBootImageFlag::FromGlobalGflags(
        parsed_flags.system_image_dir, &*android_builds_result));

    std::vector<GuestConfig> guest_configs;
    if (parsed_flags.boot_image && parsed_flags.super_image &&
        parsed_flags.vendor_boot) {
      guest_configs = CF_EXPECT(GetGuestConfigs(
          fetcher_configs, *parsed_flags.boot_image, *parsed_flags.super_image,
          parsed_flags.system_image_dir, *parsed_flags.vendor_boot));
    }

    if (!guest_configs.empty()) {
      Result<VmManagerFlag> vm_manager_result =
          VmManagerFlag::FromGlobalGflags(guest_configs);
      if (vm_manager_result) {
        parsed_flags.vm_manager.emplace(*vm_manager_result);
      }
    }
    if (!guest_configs.empty() && parsed_flags.vm_manager) {
      Result<BootloaderFlag> bootloader_flag_result =
          BootloaderFlag::FromGlobalGflags(guest_configs,
                                           parsed_flags.system_image_dir,
                                           *parsed_flags.vm_manager);
      if (bootloader_flag_result) {
        parsed_flags.bootloader.emplace(*bootloader_flag_result);
      }
    }

    if (!guest_configs.empty() && parsed_flags.vm_manager) {
      parsed_flags.gpu_mode_used =
          CF_EXPECT(GetGpuModeUsed(parsed_flags.gpu_mode_requested,
                                   *parsed_flags.vm_manager, guest_configs));
    }
  }
  return parsed_flags;
}
}  // namespace cuttlefish
