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

#include <fruit/fruit.h>
#include <gflags/gflags.h>

#include "common/libs/utils/files.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/commands/assemble_cvd/vendor_dlkm_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"
#include "host/libs/vm_manager/gem5_manager.h"

namespace cuttlefish {

using vm_manager::Gem5Manager;

class KernelRamdiskRepackerImpl : public KernelRamdiskRepacker {
 public:
  INJECT(KernelRamdiskRepackerImpl(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "KernelRamdiskRepacker"; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Enabled() const override {
    // If we are booting a protected VM, for now, assume that image repacking
    // isn't trusted. Repacking requires resigning the image and keys from an
    // android host aren't trusted.
    return !instance_.protected_vm();
  }

 protected:
  static bool RebuildDlkmAndVbmeta(const std::string& build_dir,
                                   const std::string& partition_name,
                                   const std::string& output_image,
                                   const std::string& vbmeta_image) {
    // TODO(b/149866755) For now, we assume that vendor_dlkm is ext4. Add
    // logic to handle EROFS once the feature stablizes.
    const auto tmp_output_image = output_image + ".tmp";
    if (!BuildDlkmImage(build_dir, false, partition_name, tmp_output_image)) {
      LOG(ERROR) << "Failed to build `" << partition_name << "` image from "
                 << build_dir;
      return false;
    }
    if (!MoveIfChanged(tmp_output_image, output_image)) {
      return false;
    }
    if (!BuildVbmetaImage(output_image, vbmeta_image)) {
      LOG(ERROR) << "Failed to rebuild vbmeta vendor.";
      return false;
    }
    return true;
  }
  bool RepackSuperAndVbmeta(const std::string& superimg_build_dir,
                            const std::string& vendor_dlkm_build_dir,
                            const std::string& system_dlkm_build_dir,
                            const std::string& ramdisk_path) {
    const auto ramdisk_stage_dir = instance_.instance_dir() + "/ramdisk_staged";
    if (!SplitRamdiskModules(ramdisk_path, ramdisk_stage_dir,
                             vendor_dlkm_build_dir, system_dlkm_build_dir)) {
      LOG(ERROR) << "Failed to move ramdisk modules to vendor_dlkm";
      return false;
    }
    const auto new_vendor_dlkm_img =
        superimg_build_dir + "/vendor_dlkm_repacked.img";
    if (!RebuildDlkmAndVbmeta(vendor_dlkm_build_dir, "vendor_dlkm",
                              new_vendor_dlkm_img,
                              instance_.new_vbmeta_vendor_dlkm_image())) {
      LOG(ERROR) << "Failed to build vendor_dlkm image from "
                 << vendor_dlkm_build_dir;
      return false;
    }
    const auto new_system_dlkm_img =
        superimg_build_dir + "/system_dlkm_repacked.img";
    if (!RebuildDlkmAndVbmeta(system_dlkm_build_dir, "system_dlkm",
                              new_system_dlkm_img,
                              instance_.new_vbmeta_system_dlkm_image())) {
      LOG(ERROR) << "Failed to build system_dlkm image from "
                 << system_dlkm_build_dir;
      return false;
    }
    const auto new_super_img = instance_.new_super_image();
    if (!Copy(instance_.super_image(), new_super_img)) {
      PLOG(ERROR) << "Failed to copy super image " << instance_.super_image()
                  << " to " << new_super_img;
      return false;
    }
    if (!RepackSuperWithPartition(new_super_img, new_vendor_dlkm_img,
                                  "vendor_dlkm")) {
      LOG(ERROR) << "Failed to repack super image with new vendor dlkm image.";
      return false;
    }
    if (!RepackSuperWithPartition(new_super_img, new_system_dlkm_img,
                                  "system_dlkm")) {
      LOG(ERROR) << "Failed to repack super image with new system dlkm image.";
      return false;
    }
    SetCommandLineOptionWithMode("super_image", new_super_img.c_str(),
                                 google::FlagSettingMode::SET_FLAGS_DEFAULT);
    SetCommandLineOptionWithMode(
        "vbmeta_vendor_dlkm_image",
        instance_.new_vbmeta_vendor_dlkm_image().c_str(),
        google::FlagSettingMode::SET_FLAGS_DEFAULT);
    SetCommandLineOptionWithMode(
        "vbmeta_system_dlkm_image",
        instance_.new_vbmeta_system_dlkm_image().c_str(),
        google::FlagSettingMode::SET_FLAGS_DEFAULT);
    return true;
  }
  Result<void> ResultSetup() override {
    CF_EXPECTF(FileHasContent(instance_.boot_image()), "File not found: {}",
               instance_.boot_image());
    // The init_boot partition is be optional for testing boot.img
    // with the ramdisk inside.
    if (!FileHasContent(instance_.init_boot_image())) {
      LOG(WARNING) << "File not found: " << instance_.init_boot_image();
    }

    CF_EXPECTF(FileHasContent(instance_.vendor_boot_image()),
               "File not found: {}", instance_.vendor_boot_image());

    // Repacking a boot.img doesn't work with Gem5 because the user must always
    // specify a vmlinux instead of an arm64 Image, and that file can be too
    // large to be repacked. Skip repack of boot.img on Gem5, as we need to be
    // able to extract the ramdisk.img in a later stage and so this step must
    // not fail (..and the repacked kernel wouldn't be used anyway).
    if (instance_.kernel_path().size() &&
        config_.vm_manager() != Gem5Manager::name()) {
      const std::string new_boot_image_path = instance_.new_boot_image();
      CF_EXPECT(RepackBootImage(instance_.kernel_path(), instance_.boot_image(),
                                new_boot_image_path, instance_.instance_dir()),
                "Failed to regenerate the boot image with the new kernel");
      SetCommandLineOptionWithMode("boot_image", new_boot_image_path.c_str(),
                                   google::FlagSettingMode::SET_FLAGS_DEFAULT);
    }

    if (instance_.kernel_path().size() || instance_.initramfs_path().size()) {
      const std::string new_vendor_boot_image_path =
          instance_.new_vendor_boot_image();
      // Repack the vendor boot images if kernels and/or ramdisks are passed in.
      if (instance_.initramfs_path().size()) {
        const auto superimg_build_dir = instance_.instance_dir() + "/superimg";
        const auto ramdisk_repacked =
            instance_.instance_dir() + "/ramdisk_repacked";
        CF_EXPECTF(Copy(instance_.initramfs_path(), ramdisk_repacked),
                   "Failed to copy {} to {}", instance_.initramfs_path(),
                   ramdisk_repacked);
        const auto vendor_dlkm_build_dir = superimg_build_dir + "/vendor_dlkm";
        const auto system_dlkm_build_dir = superimg_build_dir + "/system_dlkm";
        CF_EXPECT(
            RepackSuperAndVbmeta(superimg_build_dir, vendor_dlkm_build_dir,
                                 system_dlkm_build_dir, ramdisk_repacked));
        bool success = RepackVendorBootImage(
            ramdisk_repacked, instance_.vendor_boot_image(),
            new_vendor_boot_image_path, config_.assembly_dir(),
            instance_.bootconfig_supported());
        if (!success) {
          LOG(ERROR) << "Failed to regenerate the vendor boot image with the "
                        "new ramdisk";
        } else {
          // This control flow implies a kernel with all configs built in.
          // If it's just the kernel, repack the vendor boot image without a
          // ramdisk.
          CF_EXPECT(
              RepackVendorBootImageWithEmptyRamdisk(
                  instance_.vendor_boot_image(), new_vendor_boot_image_path,
                  config_.assembly_dir(), instance_.bootconfig_supported()),
              "Failed to regenerate the vendor boot image without a ramdisk");
        }
        SetCommandLineOptionWithMode(
            "vendor_boot_image", new_vendor_boot_image_path.c_str(),
            google::FlagSettingMode::SET_FLAGS_DEFAULT);
      }
    }
    return {};
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 KernelRamdiskRepacker>
KernelRamdiskRepackerComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, KernelRamdiskRepackerImpl>()
      .bind<KernelRamdiskRepacker, KernelRamdiskRepackerImpl>();
}

}  // namespace cuttlefish
