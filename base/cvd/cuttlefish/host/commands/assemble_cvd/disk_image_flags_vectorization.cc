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

#include "cuttlefish/host/commands/assemble_cvd/disk_image_flags_vectorization.h"

#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parsebool.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/assemble_cvd_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/android_efi_loader.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/bootloader.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/initramfs_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/super_image_mixer.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/instance_nums.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"

namespace cuttlefish {

Result<void> DiskImageFlagsVectorization(
    CuttlefishConfig& config, const FetcherConfig& fetcher_config,
    const AndroidEfiLoaderFlag& android_efi_loader,
    const BootImageFlag& boot_image, const BootloaderFlag& bootloader,
    const InitramfsPathFlag& initramfs_path, const KernelPathFlag& kernel_path,
    const SystemImageDirFlag& system_image_dir) {
  std::vector<std::string> super_image =
      android::base::Split(FLAGS_super_image, ",");
  std::vector<std::string> vendor_boot_image =
      android::base::Split(FLAGS_vendor_boot_image, ",");
  std::vector<std::string> vbmeta_image =
      android::base::Split(FLAGS_vbmeta_image, ",");
  std::vector<std::string> vbmeta_system_image =
      android::base::Split(FLAGS_vbmeta_system_image, ",");
  auto vbmeta_vendor_dlkm_image =
      android::base::Split(FLAGS_vbmeta_vendor_dlkm_image, ",");
  auto vbmeta_system_dlkm_image =
      android::base::Split(FLAGS_vbmeta_system_dlkm_image, ",");
  auto vvmtruststore_path = android::base::Split(FLAGS_vvmtruststore_path, ",");

  std::vector<std::string> default_target_zip_vec =
      android::base::Split(FLAGS_default_target_zip, ",");
  std::vector<std::string> system_target_zip_vec =
      android::base::Split(FLAGS_system_target_zip, ",");

  std::vector<std::string> chromeos_disk =
      android::base::Split(FLAGS_chromeos_disk, ",");
  std::vector<std::string> chromeos_kernel_path =
      android::base::Split(FLAGS_chromeos_kernel_path, ",");
  std::vector<std::string> chromeos_root_image =
      android::base::Split(FLAGS_chromeos_root_image, ",");

  std::vector<std::string> linux_kernel_path =
      android::base::Split(FLAGS_linux_kernel_path, ",");
  std::vector<std::string> linux_initramfs_path =
      android::base::Split(FLAGS_linux_initramfs_path, ",");
  std::vector<std::string> linux_root_image =
      android::base::Split(FLAGS_linux_root_image, ",");

  std::vector<std::string> fuchsia_zedboot_path =
      android::base::Split(FLAGS_fuchsia_zedboot_path, ",");
  std::vector<std::string> fuchsia_multiboot_bin_path =
      android::base::Split(FLAGS_fuchsia_multiboot_bin_path, ",");
  std::vector<std::string> fuchsia_root_image =
      android::base::Split(FLAGS_fuchsia_root_image, ",");

  std::vector<std::string> custom_partition_path =
      android::base::Split(FLAGS_custom_partition_path, ",");

  std::vector<std::string> blank_sdcard_image_mb =
      android::base::Split(FLAGS_blank_sdcard_image_mb, ",");

  std::string cur_boot_image;
  std::string cur_vendor_boot_image;
  std::string cur_super_image;
  int value{};
  int instance_index = 0;
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  for (const auto& num : instance_nums) {
    auto instance = config.ForInstance(num);
    std::string cur_boot_image = boot_image.BootImageForIndex(instance_index);
    instance.set_boot_image(cur_boot_image);
    instance.set_new_boot_image(cur_boot_image);

    instance.set_init_boot_image(system_image_dir.ForIndex(instance_index) +
                                 "/init_boot.img");
    if (instance_index >= vendor_boot_image.size()) {
      cur_vendor_boot_image = vendor_boot_image[0];
    } else {
      cur_vendor_boot_image = vendor_boot_image[instance_index];
    }
    instance.set_vendor_boot_image(cur_vendor_boot_image);
    instance.set_new_vendor_boot_image(cur_vendor_boot_image);

    if (instance_index >= vbmeta_image.size()) {
      instance.set_vbmeta_image(vbmeta_image[0]);
    } else {
      instance.set_vbmeta_image(vbmeta_image[instance_index]);
    }
    if (instance_index >= vbmeta_system_image.size()) {
      instance.set_vbmeta_system_image(vbmeta_system_image[0]);
    } else {
      instance.set_vbmeta_system_image(vbmeta_system_image[instance_index]);
    }
    if (instance_index >= vbmeta_vendor_dlkm_image.size()) {
      instance.set_vbmeta_vendor_dlkm_image(vbmeta_vendor_dlkm_image[0]);
    } else {
      instance.set_vbmeta_vendor_dlkm_image(
          vbmeta_vendor_dlkm_image[instance_index]);
    }
    if (instance_index >= vbmeta_system_dlkm_image.size()) {
      instance.set_vbmeta_system_dlkm_image(vbmeta_system_dlkm_image[0]);
    } else {
      instance.set_vbmeta_system_dlkm_image(
          vbmeta_system_dlkm_image[instance_index]);
    }
    if (instance_index >= vvmtruststore_path.size()) {
      instance.set_vvmtruststore_path(vvmtruststore_path[0]);
    } else {
      instance.set_vvmtruststore_path(vvmtruststore_path[instance_index]);
    }
    if (instance_index >= super_image.size()) {
      cur_super_image = super_image[0];
    } else {
      cur_super_image = super_image[instance_index];
    }
    instance.set_super_image(cur_super_image);
    instance.set_android_efi_loader(
        android_efi_loader.AndroidEfiLoaderForInstance(instance_index));
    if (instance_index >= chromeos_disk.size()) {
      instance.set_chromeos_disk(chromeos_disk[0]);
    } else {
      instance.set_chromeos_disk(chromeos_disk[instance_index]);
    }
    if (instance_index >= chromeos_kernel_path.size()) {
      instance.set_chromeos_kernel_path(chromeos_kernel_path[0]);
    } else {
      instance.set_chromeos_kernel_path(chromeos_kernel_path[instance_index]);
    }
    if (instance_index >= chromeos_root_image.size()) {
      instance.set_chromeos_root_image(chromeos_root_image[0]);
    } else {
      instance.set_chromeos_root_image(chromeos_root_image[instance_index]);
    }
    if (instance_index >= linux_kernel_path.size()) {
      instance.set_linux_kernel_path(linux_kernel_path[0]);
    } else {
      instance.set_linux_kernel_path(linux_kernel_path[instance_index]);
    }
    if (instance_index >= linux_initramfs_path.size()) {
      instance.set_linux_initramfs_path(linux_initramfs_path[0]);
    } else {
      instance.set_linux_initramfs_path(linux_initramfs_path[instance_index]);
    }
    if (instance_index >= linux_root_image.size()) {
      instance.set_linux_root_image(linux_root_image[0]);
    } else {
      instance.set_linux_root_image(linux_root_image[instance_index]);
    }
    if (instance_index >= fuchsia_zedboot_path.size()) {
      instance.set_fuchsia_zedboot_path(fuchsia_zedboot_path[0]);
    } else {
      instance.set_fuchsia_zedboot_path(fuchsia_zedboot_path[instance_index]);
    }
    if (instance_index >= fuchsia_multiboot_bin_path.size()) {
      instance.set_fuchsia_multiboot_bin_path(fuchsia_multiboot_bin_path[0]);
    } else {
      instance.set_fuchsia_multiboot_bin_path(
          fuchsia_multiboot_bin_path[instance_index]);
    }
    if (instance_index >= fuchsia_root_image.size()) {
      instance.set_fuchsia_root_image(fuchsia_root_image[0]);
    } else {
      instance.set_fuchsia_root_image(fuchsia_root_image[instance_index]);
    }
    if (instance_index >= custom_partition_path.size()) {
      instance.set_custom_partition_path(custom_partition_path[0]);
    } else {
      instance.set_custom_partition_path(custom_partition_path[instance_index]);
    }
    instance.set_bootloader(bootloader.BootloaderForInstance(instance_index));
    instance.set_kernel_path(kernel_path.KernelPathForIndex(instance_index));
    instance.set_initramfs_path(
        initramfs_path.InitramfsPathForIndex(instance_index));

    using android::base::ParseInt;

    if (instance_index >= blank_sdcard_image_mb.size()) {
      CF_EXPECTF(ParseInt(blank_sdcard_image_mb[0], &value), "'{}'",
                 blank_sdcard_image_mb[0]);
    } else {
      CF_EXPECTF(ParseInt(blank_sdcard_image_mb[instance_index], &value),
                 "'{}'", blank_sdcard_image_mb[instance_index]);
    }
    instance.set_blank_sdcard_image_mb(value);

    // Repacking a boot.img changes boot_image and vendor_boot_image paths
    const CuttlefishConfig& const_config =
        const_cast<const CuttlefishConfig&>(config);
    const CuttlefishConfig::InstanceSpecific const_instance =
        const_config.ForInstance(num);
    if (!kernel_path.KernelPathForIndex(instance_index).empty() &&
        config.vm_manager() != VmmMode::kGem5) {
      const std::string new_boot_image_path =
          const_instance.PerInstancePath("boot_repacked.img");
      // change the new flag value to corresponding instance
      instance.set_new_boot_image(new_boot_image_path.c_str());
    }

    instance.set_data_image(system_image_dir.ForIndex(instance_index) +
                            "/userdata.img");
    instance.set_new_data_image(const_instance.PerInstancePath("userdata.img"));

    bool has_kernel = !kernel_path.KernelPathForIndex(instance_index).empty();
    bool has_initramfs =
        !initramfs_path.InitramfsPathForIndex(instance_index).empty();
    if (has_kernel || has_initramfs) {
      const std::string new_vendor_boot_image_path =
          const_instance.PerInstancePath("vendor_boot_repacked.img");
      // Repack the vendor boot images if kernels and/or ramdisks are passed in.
      if (has_initramfs) {
        // change the new flag value to corresponding instance
        instance.set_new_vendor_boot_image(new_vendor_boot_image_path.c_str());
      }
    }

    if (instance_index >= default_target_zip_vec.size()) {
      instance.set_default_target_zip(default_target_zip_vec[0]);
    } else {
      instance.set_default_target_zip(default_target_zip_vec[instance_index]);
    }
    if (instance_index >= system_target_zip_vec.size()) {
      instance.set_system_target_zip(system_target_zip_vec[0]);
    } else {
      instance.set_system_target_zip(system_target_zip_vec[instance_index]);
    }

    // We will need to rebuild vendor_dlkm if custom ramdisk is specified, as a
    // result super image would need to be rebuilt as well.
    if (CF_EXPECT(SuperImageNeedsRebuilding(
            fetcher_config, const_instance.default_target_zip(),
            const_instance.system_target_zip())) ||
        has_initramfs) {
      const std::string new_super_image_path =
          const_instance.PerInstancePath("super.img");
      instance.set_new_super_image(new_super_image_path);
      const std::string new_vbmeta_image_path =
          const_instance.PerInstancePath("os_vbmeta.img");
      instance.set_new_vbmeta_image(new_vbmeta_image_path);
    }

    instance.set_new_vbmeta_vendor_dlkm_image(
        const_instance.PerInstancePath("vbmeta_vendor_dlkm_repacked.img"));
    instance.set_new_vbmeta_system_dlkm_image(
        const_instance.PerInstancePath("vbmeta_system_dlkm_repacked.img"));

    instance_index++;
  }
  return {};
}

}  // namespace cuttlefish
