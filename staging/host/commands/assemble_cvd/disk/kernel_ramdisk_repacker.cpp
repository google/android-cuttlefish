/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/assemble_cvd/disk/disk.h"

#include <string>

#include "common/libs/utils/files.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/commands/assemble_cvd/vendor_dlkm_utils.h"
#include "host/libs/avb/avb.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

Result<void> RebuildDlkmAndVbmeta(const std::string& build_dir,
                                  const std::string& partition_name,
                                  const std::string& output_image,
                                  const std::string& vbmeta_image) {
  // TODO(b/149866755) For now, we assume that vendor_dlkm is ext4. Add
  // logic to handle EROFS once the feature stabilizes.
  const auto tmp_output_image = output_image + ".tmp";
  CF_EXPECTF(BuildDlkmImage(build_dir, false, partition_name, tmp_output_image),
             "Failed to build `{}' image from '{}'", partition_name, build_dir);

  CF_EXPECT(MoveIfChanged(tmp_output_image, output_image));

  CF_EXPECT(BuildVbmetaImage(output_image, vbmeta_image),
            "Failed to rebuild vbmeta vendor.");

  return {};
}

Result<void> RepackSuperAndVbmeta(
    const CuttlefishConfig::InstanceSpecific& instance,
    const std::string& superimg_build_dir,
    const std::string& vendor_dlkm_build_dir,
    const std::string& system_dlkm_build_dir, const std::string& ramdisk_path) {
  const auto ramdisk_stage_dir = instance.instance_dir() + "/ramdisk_staged";
  CF_EXPECT(SplitRamdiskModules(ramdisk_path, ramdisk_stage_dir,
                                vendor_dlkm_build_dir, system_dlkm_build_dir),
            "Failed to move ramdisk modules to vendor_dlkm");

  const auto new_vendor_dlkm_img =
      superimg_build_dir + "/vendor_dlkm_repacked.img";
  CF_EXPECTF(RebuildDlkmAndVbmeta(vendor_dlkm_build_dir, "vendor_dlkm",
                                  new_vendor_dlkm_img,
                                  instance.new_vbmeta_vendor_dlkm_image()),
             "Failed to build vendor_dlkm image from '{}'",
             vendor_dlkm_build_dir);

  const auto new_system_dlkm_img =
      superimg_build_dir + "/system_dlkm_repacked.img";
  CF_EXPECTF(RebuildDlkmAndVbmeta(system_dlkm_build_dir, "system_dlkm",
                                  new_system_dlkm_img,
                                  instance.new_vbmeta_system_dlkm_image()),
             "Failed to build system_dlkm image from '{}'",
             system_dlkm_build_dir);

  const auto new_super_img = instance.new_super_image();
  CF_EXPECTF(Copy(instance.super_image(), new_super_img),
             "Failed to copy super image '{}' to '{}': '{}'",
             instance.super_image(), new_super_img, strerror(errno));

  CF_EXPECT(RepackSuperWithPartition(new_super_img, new_vendor_dlkm_img,
                                     "vendor_dlkm"),
            "Failed to repack super image with new vendor dlkm image.");

  CF_EXPECT(RepackSuperWithPartition(new_super_img, new_system_dlkm_img,
                                     "system_dlkm"),
            "Failed to repack super image with new system dlkm image.");

  return {};
}

}  // namespace

Result<void> RepackKernelRamdisk(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance, const Avb& avb) {
  if (instance.protected_vm()) {
    // If we are booting a protected VM, for now, assume that image repacking
    // isn't trusted. Repacking requires resigning the image and keys from an
    // android host aren't trusted.
    return {};
  }

  CF_EXPECTF(FileHasContent(instance.boot_image()), "File not found: {}",
             instance.boot_image());
  // The init_boot partition is be optional for testing boot.img
  // with the ramdisk inside.
  if (!FileHasContent(instance.init_boot_image())) {
    LOG(WARNING) << "File not found: " << instance.init_boot_image();
  }

  CF_EXPECTF(FileHasContent(instance.vendor_boot_image()), "File not found: {}",
             instance.vendor_boot_image());

  // Repacking a boot.img doesn't work with Gem5 because the user must always
  // specify a vmlinux instead of an arm64 Image, and that file can be too
  // large to be repacked. Skip repack of boot.img on Gem5, as we need to be
  // able to extract the ramdisk.img in a later stage and so this step must
  // not fail (..and the repacked kernel wouldn't be used anyway).
  if (instance.kernel_path().size() && config.vm_manager() != VmmMode::kGem5) {
    CF_EXPECT(
        RepackBootImage(avb, instance.kernel_path(), instance.boot_image(),
                        instance.new_boot_image(), instance.instance_dir()),
        "Failed to regenerate the boot image with the new kernel");
  }

  if (instance.kernel_path().size() || instance.initramfs_path().size()) {
    const std::string new_vendor_boot_image_path =
        instance.new_vendor_boot_image();
    // Repack the vendor boot images if kernels and/or ramdisks are passed in.
    if (instance.initramfs_path().size()) {
      const auto superimg_build_dir = instance.instance_dir() + "/superimg";
      const auto ramdisk_repacked =
          instance.instance_dir() + "/ramdisk_repacked";
      CF_EXPECTF(Copy(instance.initramfs_path(), ramdisk_repacked),
                 "Failed to copy {} to {}", instance.initramfs_path(),
                 ramdisk_repacked);
      const auto vendor_dlkm_build_dir = superimg_build_dir + "/vendor_dlkm";
      const auto system_dlkm_build_dir = superimg_build_dir + "/system_dlkm";
      CF_EXPECT(RepackSuperAndVbmeta(instance, superimg_build_dir,
                                     vendor_dlkm_build_dir,
                                     system_dlkm_build_dir, ramdisk_repacked));
      bool success = RepackVendorBootImage(
          ramdisk_repacked, instance.vendor_boot_image(),
          new_vendor_boot_image_path, config.assembly_dir(),
          instance.bootconfig_supported());
      if (!success) {
        LOG(ERROR) << "Failed to regenerate the vendor boot image with the "
                      "new ramdisk";
      } else {
        // This control flow implies a kernel with all configs built in.
        // If it's just the kernel, repack the vendor boot image without a
        // ramdisk.
        CF_EXPECT(
            RepackVendorBootImageWithEmptyRamdisk(
                instance.vendor_boot_image(), new_vendor_boot_image_path,
                config.assembly_dir(), instance.bootconfig_supported()),
            "Failed to regenerate the vendor boot image without a ramdisk");
      }
    }
  }
  return {};
}

}  // namespace cuttlefish
