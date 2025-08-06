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

#include "cuttlefish/host/commands/assemble_cvd/create_dynamic_disk_files.h"

#include <sys/statvfs.h>

#include <string>

#include <android-base/logging.h>
#include <android-base/parsebool.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_config.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_image_utils.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/access_kregistry.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/ap_composite_disk.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/bootloader_present.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/chromeos_state.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/factory_reset_protected.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/gem5_image_unpacker.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_bootconfig.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_vbmeta.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/hwcomposer_pmem.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/initialize_instance_composite_disk.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/kernel_ramdisk_repacker.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/os_composite_disk.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/pflash.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/pstore.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/sd_card.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/vbmeta_enforce_minimum_size.h"
#include "cuttlefish/host/commands/assemble_cvd/disk_builder.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/super_image_mixer.h"
#include "cuttlefish/host/libs/avb/avb.h"
#include "cuttlefish/host/libs/config/ap_boot_flow.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"

namespace cuttlefish {

static uint64_t AvailableSpaceAtPath(const std::string& path) {
  struct statvfs vfs{};
  if (statvfs(path.c_str(), &vfs) != 0) {
    int error_num = errno;
    LOG(ERROR) << "Could not find space available at " << path << ", error was "
               << strerror(error_num);
    return 0;
  }
  // f_frsize (block size) * f_bavail (free blocks) for unprivileged users.
  return static_cast<uint64_t>(vfs.f_frsize) * vfs.f_bavail;
}

Result<void> CreateDynamicDiskFiles(
    const FetcherConfig& fetcher_config, const CuttlefishConfig& config,
    const SystemImageDirFlag& system_image_dir) {
  for (const auto& instance : config.Instances()) {
    std::optional<ChromeOsStateImage> chrome_os_state =
        CF_EXPECT(ChromeOsStateImage::CreateIfNecessary(instance));

    CF_EXPECT(RepackKernelRamdisk(config, instance, Avb()));
    CF_EXPECT(VbmetaEnforceMinimumSize(instance));
    CF_EXPECT(BootloaderPresentCheck(instance));
    CF_EXPECT(Gem5ImageUnpacker(config));  // Requires RepackKernelRamdisk
    CF_EXPECT(InitializeEspImage(config, instance));
    CF_EXPECT(RebuildSuperImageIfNecessary(fetcher_config, instance));

    CF_EXPECT(InitializeAccessKregistryImage(instance));
    CF_EXPECT(InitializeHwcomposerPmemImage(instance));
    CF_EXPECT(InitializePstore(instance));
    CF_EXPECT(InitializeSdCard(config, instance));
    CF_EXPECT(InitializeDataImage(instance));
    CF_EXPECT(InitializePflash(instance));

    // Check if filling in the sparse image would run out of disk space.
    std::string data_image = instance.data_image();
    auto existing_sizes = SparseFileSizes(data_image);
    if (existing_sizes.sparse_size == 0 && existing_sizes.disk_size == 0) {
      data_image = instance.new_data_image();
      existing_sizes = SparseFileSizes(data_image);
      CF_EXPECT(existing_sizes.sparse_size > 0 || existing_sizes.disk_size > 0,
                "Unable to determine size of \""
                    << data_image << "\". Does this file exist?");
    }
    if (existing_sizes.sparse_size > 0 || existing_sizes.disk_size > 0) {
      auto available_space = AvailableSpaceAtPath(data_image);
      if (available_space <
          existing_sizes.sparse_size - existing_sizes.disk_size) {
        // TODO(schuffelen): Duplicate this check in run_cvd when it can run on
        // a separate machine
        return CF_ERR("Not enough space remaining in fs containing \""
                      << data_image << "\", wanted "
                      << (existing_sizes.sparse_size - existing_sizes.disk_size)
                      << ", got " << available_space);
      } else {
        LOG(DEBUG) << "Available space: " << available_space;
        LOG(DEBUG) << "Sparse size of \"" << data_image
                   << "\": " << existing_sizes.sparse_size;
        LOG(DEBUG) << "Disk size of \"" << data_image
                   << "\": " << existing_sizes.disk_size;
      }
    }

    MetadataImage metadata = CF_EXPECT(MetadataImage::ReuseOrCreate(instance));
    MiscImage misc = CF_EXPECT(MiscImage::ReuseOrCreate(instance));

    DiskBuilder os_disk_builder = OsCompositeDiskBuilder(
        config, instance, chrome_os_state, metadata, misc, system_image_dir);
    const auto os_built_composite =
        CF_EXPECT(os_disk_builder.BuildCompositeDiskIfNecessary());

    BootloaderEnvPartition bootloader_env_partition =
        CF_EXPECT(BootloaderEnvPartition::Create(config, instance));

    std::optional<ApBootloaderEnvPartition> ap_bootloader_env_partition =
        CF_EXPECT(ApBootloaderEnvPartition::Create(config, instance));

    FactoryResetProtectedImage factory_reset_protected =
        CF_EXPECT(FactoryResetProtectedImage::Create(instance));

    std::optional<BootConfigPartition> boot_config =
        CF_EXPECT(BootConfigPartition::CreateIfNeeded(config, instance));

    PersistentVbmeta persistent_vbmeta = CF_EXPECT(PersistentVbmeta::Create(
        boot_config, bootloader_env_partition, instance));

    std::optional<ApPersistentVbmeta> ap_persistent_vbmeta =
        ap_bootloader_env_partition.has_value()
            ? CF_EXPECT(ApPersistentVbmeta::Create(*ap_bootloader_env_partition,
                                                   boot_config, instance))
            : std::nullopt;

    FactoryResetProtectedImage factory_reset_protected_image =
        CF_EXPECT(FactoryResetProtectedImage::Create(instance));

    // TODO: schuffelen - do something with these types
    CF_EXPECT(InstanceCompositeDisk::Create(boot_config, config, instance,
                                            factory_reset_protected,
                                            persistent_vbmeta));
    CF_EXPECT(ApCompositeDisk::Create(ap_persistent_vbmeta, config, instance));

    auto ap_disk_builder = ApCompositeDiskBuilder(config, instance);
    if (instance.ap_boot_flow() != APBootFlow::None) {
      CF_EXPECT(ap_disk_builder.BuildCompositeDiskIfNecessary());
    }

    if (os_built_composite) {
      if (FileExists(instance.access_kregistry_path())) {
        CF_EXPECT(CreateBlankImage(instance.access_kregistry_path(), 2 /* mb */,
                                   "none"),
                  "Failed for \"" << instance.access_kregistry_path() << "\"");
      }
      if (FileExists(instance.hwcomposer_pmem_path())) {
        CF_EXPECT(CreateBlankImage(instance.hwcomposer_pmem_path(), 2 /* mb */,
                                   "none"),
                  "Failed for \"" << instance.hwcomposer_pmem_path() << "\"");
      }
      if (FileExists(instance.pstore_path())) {
        CF_EXPECT(CreateBlankImage(instance.pstore_path(), 2 /* mb */, "none"),
                  "Failed for\"" << instance.pstore_path() << "\"");
      }
    }

    os_disk_builder.OverlayPath(instance.PerInstancePath("overlay.img"));
    CF_EXPECT(os_disk_builder.BuildOverlayIfNecessary());
    if (instance.ap_boot_flow() != APBootFlow::None) {
      ap_disk_builder.OverlayPath(instance.PerInstancePath("ap_overlay.img"));
      CF_EXPECT(ap_disk_builder.BuildOverlayIfNecessary());
    }

    // Check that the files exist
    for (const auto& file : instance.virtual_disk_paths()) {
      if (!file.empty()) {
        CF_EXPECT(FileHasContent(file), "File not found: \"" << file << "\"");
      }
    }

    // Gem5 Simulate per-instance what the bootloader would usually do
    // Since on other devices this runs every time, just do it here every time
    if (config.vm_manager() == VmmMode::kGem5) {
      RepackGem5BootImage(instance.PerInstancePath("initrd.img"), boot_config,
                          config.assembly_dir(), instance.initramfs_path());
    }
  }

  return {};
}

}  // namespace cuttlefish
