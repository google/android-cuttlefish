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

#include "host/commands/assemble_cvd/disk_flags.h"

#include <android-base/logging.h>
#include <android-base/parsebool.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <gflags/gflags.h>
#include <sys/statvfs.h>

#include <fstream>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/commands/assemble_cvd/disk/disk.h"
#include "host/commands/assemble_cvd/disk_builder.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/assemble_cvd/super_image_mixer.h"
#include "host/commands/assemble_cvd/vendor_dlkm_utils.h"
#include "host/libs/config/bootconfig_args.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/data_image.h"
#include "host/libs/config/inject.h"
#include "host/libs/config/instance_nums.h"
#include "host/libs/vm_manager/gem5_manager.h"

DECLARE_string(system_image_dir);

DEFINE_string(boot_image, CF_DEFAULTS_BOOT_IMAGE,
              "Location of cuttlefish boot image. If empty it is assumed to be "
              "boot.img in the directory specified by -system_image_dir.");
DEFINE_string(
    init_boot_image, CF_DEFAULTS_INIT_BOOT_IMAGE,
    "Location of cuttlefish init boot image. If empty it is assumed to "
    "be init_boot.img in the directory specified by -system_image_dir.");
DEFINE_string(data_image, CF_DEFAULTS_DATA_IMAGE,
              "Location of the data partition image.");
DEFINE_string(super_image, CF_DEFAULTS_SUPER_IMAGE,
              "Location of the super partition image.");
DEFINE_string(misc_image, CF_DEFAULTS_MISC_IMAGE,
              "Location of the misc partition image. If the image does not "
              "exist, a blank new misc partition image is created.");
DEFINE_string(misc_info_txt, "", "Location of the misc_info.txt file.");
DEFINE_string(metadata_image, CF_DEFAULTS_METADATA_IMAGE,
              "Location of the metadata partition image "
              "to be generated.");
DEFINE_string(
    vendor_boot_image, CF_DEFAULTS_VENDOR_BOOT_IMAGE,
    "Location of cuttlefish vendor boot image. If empty it is assumed to "
    "be vendor_boot.img in the directory specified by -system_image_dir.");
DEFINE_string(vbmeta_image, CF_DEFAULTS_VBMETA_IMAGE,
              "Location of cuttlefish vbmeta image. If empty it is assumed to "
              "be vbmeta.img in the directory specified by -system_image_dir.");
DEFINE_string(
    vbmeta_system_image, CF_DEFAULTS_VBMETA_SYSTEM_IMAGE,
    "Location of cuttlefish vbmeta_system image. If empty it is assumed to "
    "be vbmeta_system.img in the directory specified by -system_image_dir.");
DEFINE_string(
    vbmeta_vendor_dlkm_image, CF_DEFAULTS_VBMETA_VENDOR_DLKM_IMAGE,
    "Location of cuttlefish vbmeta_vendor_dlkm image. If empty it is assumed "
    "to "
    "be vbmeta_vendor_dlkm.img in the directory specified by "
    "-system_image_dir.");
DEFINE_string(
    vbmeta_system_dlkm_image, CF_DEFAULTS_VBMETA_SYSTEM_DLKM_IMAGE,
    "Location of cuttlefish vbmeta_system_dlkm image. If empty it is assumed "
    "to "
    "be vbmeta_system_dlkm.img in the directory specified by "
    "-system_image_dir.");

DEFINE_string(
    default_target_zip, CF_DEFAULTS_DEFAULT_TARGET_ZIP,
    "Location of default target zip file.");
DEFINE_string(
    system_target_zip, CF_DEFAULTS_SYSTEM_TARGET_ZIP,
    "Location of system target zip file.");

DEFINE_string(linux_kernel_path, CF_DEFAULTS_LINUX_KERNEL_PATH,
              "Location of linux kernel for cuttlefish otheros flow.");
DEFINE_string(linux_initramfs_path, CF_DEFAULTS_LINUX_INITRAMFS_PATH,
              "Location of linux initramfs.img for cuttlefish otheros flow.");
DEFINE_string(linux_root_image, CF_DEFAULTS_LINUX_ROOT_IMAGE,
              "Location of linux root filesystem image for cuttlefish otheros flow.");

DEFINE_string(fuchsia_zedboot_path, CF_DEFAULTS_FUCHSIA_ZEDBOOT_PATH,
              "Location of fuchsia zedboot path for cuttlefish otheros flow.");
DEFINE_string(fuchsia_multiboot_bin_path, CF_DEFAULTS_FUCHSIA_MULTIBOOT_BIN_PATH,
              "Location of fuchsia multiboot bin path for cuttlefish otheros flow.");
DEFINE_string(fuchsia_root_image, CF_DEFAULTS_FUCHSIA_ROOT_IMAGE,
              "Location of fuchsia root filesystem image for cuttlefish otheros flow.");

DEFINE_string(custom_partition_path, CF_DEFAULTS_CUSTOM_PARTITION_PATH,
              "Location of custom image that will be passed as a \"custom\" partition"
              "to rootfs and can be used by /dev/block/by-name/custom");

DEFINE_string(blank_metadata_image_mb, CF_DEFAULTS_BLANK_METADATA_IMAGE_MB,
              "The size of the blank metadata image to generate, MB.");
DEFINE_string(
    blank_sdcard_image_mb, CF_DEFAULTS_BLANK_SDCARD_IMAGE_MB,
    "If enabled, the size of the blank sdcard image to generate, MB.");

DECLARE_string(ap_rootfs_image);
DECLARE_string(bootloader);
DECLARE_string(initramfs_path);
DECLARE_string(kernel_path);
DECLARE_bool(resume);
DECLARE_bool(use_overlay);
DECLARE_bool(use_16k);

namespace cuttlefish {

using APBootFlow = CuttlefishConfig::InstanceSpecific::APBootFlow;
using vm_manager::Gem5Manager;

Result<void> ResolveInstanceFiles() {
  CF_EXPECT(!FLAGS_system_image_dir.empty(),
            "--system_image_dir must be specified.");
  if (FLAGS_use_16k) {
    CF_EXPECT(FLAGS_kernel_path.empty(),
              "--use_16k is not compatible with --kernel_path");
    CF_EXPECT(FLAGS_initramfs_path.empty(),
              "--use_16k is not compatible with --initramfs_path");
  }

  std::vector<std::string> system_image_dir =
      android::base::Split(FLAGS_system_image_dir, ",");
  std::string default_boot_image = "";
  std::string default_init_boot_image = "";
  std::string default_data_image = "";
  std::string default_metadata_image = "";
  std::string default_super_image = "";
  std::string default_misc_image = "";
  std::string default_misc_info_txt = "";
  std::string default_vendor_boot_image = "";
  std::string default_vbmeta_image = "";
  std::string default_vbmeta_system_image = "";
  std::string default_vbmeta_vendor_dlkm_image = "";
  std::string default_vbmeta_system_dlkm_image = "";
  std::string default_16k_kernel_image = "";
  std::string default_16k_ramdisk_image = "";

  std::string cur_system_image_dir;
  std::string comma_str = "";
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  for (int instance_index = 0; instance_index < instance_nums.size(); instance_index++) {
    if (instance_index < system_image_dir.size()) {
      cur_system_image_dir = system_image_dir[instance_index];
    } else {
      // legacy variable or out of boundary. Vectorize by copy [0] to all instances
      cur_system_image_dir = system_image_dir[0];
    }
    if (instance_index > 0) {
      comma_str = ",";
    }

    // If user did not specify location of either of these files, expect them to
    // be placed in --system_image_dir location.
    default_boot_image += comma_str + cur_system_image_dir + "/boot.img";
    default_init_boot_image += comma_str + cur_system_image_dir + "/init_boot.img";
    default_data_image += comma_str + cur_system_image_dir + "/userdata.img";
    default_metadata_image += comma_str + cur_system_image_dir + "/metadata.img";
    default_super_image += comma_str + cur_system_image_dir + "/super.img";
    default_misc_image += comma_str + cur_system_image_dir + "/misc.img";
    default_misc_info_txt +=
        comma_str + cur_system_image_dir + "/misc_info.txt";
    default_vendor_boot_image += comma_str + cur_system_image_dir + "/vendor_boot.img";
    default_vbmeta_image += comma_str + cur_system_image_dir + "/vbmeta.img";
    default_vbmeta_system_image += comma_str + cur_system_image_dir + "/vbmeta_system.img";
    default_vbmeta_vendor_dlkm_image +=
        comma_str + cur_system_image_dir + "/vbmeta_vendor_dlkm.img";
    default_vbmeta_system_dlkm_image +=
        comma_str + cur_system_image_dir + "/vbmeta_system_dlkm.img";
    if (FLAGS_use_16k) {
      const auto kernel_16k = cur_system_image_dir + "/kernel_16k";
      const auto ramdisk_16k = cur_system_image_dir + "/ramdisk_16k.img";
      default_16k_kernel_image += comma_str + kernel_16k;
      default_16k_ramdisk_image += comma_str + ramdisk_16k;
      CF_EXPECT(FileExists(kernel_16k),
                kernel_16k + " missing for launching 16k cuttlefish");
      CF_EXPECT(FileExists(ramdisk_16k),
                ramdisk_16k + " missing for launching 16k cuttlefish");
    }
  }
  if (FLAGS_use_16k) {
    LOG(INFO) << "Using 16k kernel: " << default_16k_kernel_image;
    LOG(INFO) << "Using 16k ramdisk: " << default_16k_ramdisk_image;

    SetCommandLineOptionWithMode("kernel_path",
                                 default_16k_kernel_image.c_str(),
                                 google::FlagSettingMode::SET_FLAGS_DEFAULT);
    SetCommandLineOptionWithMode("initramfs_path",
                                 default_16k_ramdisk_image.c_str(),
                                 google::FlagSettingMode::SET_FLAGS_DEFAULT);
  }
  SetCommandLineOptionWithMode("boot_image", default_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("init_boot_image",
                               default_init_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("data_image", default_data_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("metadata_image", default_metadata_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("super_image", default_super_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("misc_image", default_misc_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("misc_info_txt", default_misc_info_txt.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vendor_boot_image",
                               default_vendor_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vbmeta_image", default_vbmeta_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vbmeta_system_image",
                               default_vbmeta_system_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vbmeta_vendor_dlkm_image",
                               default_vbmeta_vendor_dlkm_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vbmeta_system_dlkm_image",
                               default_vbmeta_system_dlkm_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  return {};
}

std::vector<ImagePartition> linux_composite_disk_config(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<ImagePartition> partitions;

  partitions.push_back(ImagePartition{
      .label = "linux_esp",
      .image_file_path = AbsolutePath(instance.otheros_esp_image_path()),
      .type = kEfiSystemPartition,
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "linux_root",
      .image_file_path = AbsolutePath(instance.linux_root_image()),
      .read_only = FLAGS_use_overlay,
  });

  return partitions;
}

std::vector<ImagePartition> fuchsia_composite_disk_config(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<ImagePartition> partitions;

  partitions.push_back(ImagePartition{
      .label = "fuchsia_esp",
      .image_file_path = AbsolutePath(instance.otheros_esp_image_path()),
      .type = kEfiSystemPartition,
      .read_only = FLAGS_use_overlay,
  });

  return partitions;
}

std::vector<ImagePartition> android_composite_disk_config(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<ImagePartition> partitions;

  partitions.push_back(ImagePartition{
      .label = "misc",
      .image_file_path = AbsolutePath(instance.new_misc_image()),
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "boot_a",
      .image_file_path = AbsolutePath(instance.new_boot_image()),
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "boot_b",
      .image_file_path = AbsolutePath(instance.new_boot_image()),
      .read_only = FLAGS_use_overlay,
  });
  const auto init_boot_path = instance.init_boot_image();
  if (FileExists(init_boot_path)) {
    partitions.push_back(ImagePartition{
        .label = "init_boot_a",
        .image_file_path = AbsolutePath(init_boot_path),
        .read_only = FLAGS_use_overlay,
    });
    partitions.push_back(ImagePartition{
        .label = "init_boot_b",
        .image_file_path = AbsolutePath(init_boot_path),
        .read_only = FLAGS_use_overlay,
    });
  }
  partitions.push_back(ImagePartition{
      .label = "vendor_boot_a",
      .image_file_path = AbsolutePath(instance.new_vendor_boot_image()),
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "vendor_boot_b",
      .image_file_path = AbsolutePath(instance.new_vendor_boot_image()),
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_a",
      .image_file_path = AbsolutePath(instance.vbmeta_image()),
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_b",
      .image_file_path = AbsolutePath(instance.vbmeta_image()),
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_system_a",
      .image_file_path = AbsolutePath(instance.vbmeta_system_image()),
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_system_b",
      .image_file_path = AbsolutePath(instance.vbmeta_system_image()),
      .read_only = FLAGS_use_overlay,
  });
  auto vbmeta_vendor_dlkm_img = instance.new_vbmeta_vendor_dlkm_image();
  if (!FileExists(vbmeta_vendor_dlkm_img)) {
    vbmeta_vendor_dlkm_img = instance.vbmeta_vendor_dlkm_image();
  }
  if (FileExists(vbmeta_vendor_dlkm_img)) {
    partitions.push_back(ImagePartition{
        .label = "vbmeta_vendor_dlkm_a",
        .image_file_path = AbsolutePath(vbmeta_vendor_dlkm_img),
        .read_only = FLAGS_use_overlay,
    });
    partitions.push_back(ImagePartition{
        .label = "vbmeta_vendor_dlkm_b",
        .image_file_path = AbsolutePath(vbmeta_vendor_dlkm_img),
        .read_only = FLAGS_use_overlay,
    });
  }
  auto vbmeta_system_dlkm_img = instance.new_vbmeta_system_dlkm_image();
  if (!FileExists(vbmeta_system_dlkm_img)) {
    vbmeta_system_dlkm_img = instance.vbmeta_system_dlkm_image();
  }
  if (FileExists(vbmeta_system_dlkm_img)) {
    partitions.push_back(ImagePartition{
        .label = "vbmeta_system_dlkm_a",
        .image_file_path = AbsolutePath(vbmeta_system_dlkm_img),
        .read_only = FLAGS_use_overlay,
    });
    partitions.push_back(ImagePartition{
        .label = "vbmeta_system_dlkm_b",
        .image_file_path = AbsolutePath(vbmeta_system_dlkm_img),
        .read_only = FLAGS_use_overlay,
    });
  }
  auto super_image = instance.new_super_image();
  if (!FileExists(super_image)) {
    super_image = instance.super_image();
  }
  partitions.push_back(ImagePartition{
      .label = "super",
      .image_file_path = AbsolutePath(super_image),
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "userdata",
      .image_file_path = AbsolutePath(instance.data_image()),
      .read_only = FLAGS_use_overlay,
  });
  partitions.push_back(ImagePartition{
      .label = "metadata",
      .image_file_path = AbsolutePath(instance.new_metadata_image()),
      .read_only = FLAGS_use_overlay,
  });
  const auto custom_partition_path = instance.custom_partition_path();
  if (!custom_partition_path.empty()) {
    partitions.push_back(ImagePartition{
        .label = "custom",
        .image_file_path = AbsolutePath(custom_partition_path),
        .read_only = FLAGS_use_overlay,
    });
  }

  return partitions;
}

std::vector<ImagePartition> GetApCompositeDiskConfig(const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<ImagePartition> partitions;

  if (instance.ap_boot_flow() == APBootFlow::Grub) {
    partitions.push_back(ImagePartition{
        .label = "ap_esp",
        .image_file_path = AbsolutePath(instance.ap_esp_image_path()),
        .read_only = FLAGS_use_overlay,
    });
  }

  partitions.push_back(ImagePartition{
      .label = "ap_rootfs",
      .image_file_path = AbsolutePath(config.ap_rootfs_image()),
      .read_only = FLAGS_use_overlay,
  });

  return partitions;
}

std::vector<ImagePartition> GetOsCompositeDiskConfig(
    const CuttlefishConfig::InstanceSpecific& instance) {

  switch (instance.boot_flow()) {
    case CuttlefishConfig::InstanceSpecific::BootFlow::Android:
      return android_composite_disk_config(instance);
      break;
    case CuttlefishConfig::InstanceSpecific::BootFlow::Linux:
      return linux_composite_disk_config(instance);
      break;
    case CuttlefishConfig::InstanceSpecific::BootFlow::Fuchsia:
      return fuchsia_composite_disk_config(instance);
      break;
  }
}

DiskBuilder OsCompositeDiskBuilder(const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  return DiskBuilder()
      .Partitions(GetOsCompositeDiskConfig(instance))
      .VmManager(config.vm_manager())
      .CrosvmPath(instance.crosvm_binary())
      .ConfigPath(instance.PerInstancePath("os_composite_disk_config.txt"))
      .HeaderPath(instance.PerInstancePath("os_composite_gpt_header.img"))
      .FooterPath(instance.PerInstancePath("os_composite_gpt_footer.img"))
      .CompositeDiskPath(instance.os_composite_disk_path())
      .ResumeIfPossible(FLAGS_resume);
}

DiskBuilder ApCompositeDiskBuilder(const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  return DiskBuilder()
      .Partitions(GetApCompositeDiskConfig(config, instance))
      .VmManager(config.vm_manager())
      .CrosvmPath(instance.crosvm_binary())
      .ConfigPath(instance.PerInstancePath("ap_composite_disk_config.txt"))
      .HeaderPath(instance.PerInstancePath("ap_composite_gpt_header.img"))
      .FooterPath(instance.PerInstancePath("ap_composite_gpt_footer.img"))
      .CompositeDiskPath(instance.ap_composite_disk_path())
      .ResumeIfPossible(FLAGS_resume);
}

static uint64_t AvailableSpaceAtPath(const std::string& path) {
  struct statvfs vfs {};
  if (statvfs(path.c_str(), &vfs) != 0) {
    int error_num = errno;
    LOG(ERROR) << "Could not find space available at " << path << ", error was "
               << strerror(error_num);
    return 0;
  }
  // f_frsize (block size) * f_bavail (free blocks) for unprivileged users.
  return static_cast<uint64_t>(vfs.f_frsize) * vfs.f_bavail;
}

class InitializeMetadataImage : public SetupFeature {
 public:
  INJECT(InitializeMetadataImage(
      const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeMetadataImage"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (FileExists(instance_.new_metadata_image()) &&
        FileSize(instance_.new_metadata_image()) == instance_.blank_metadata_image_mb() << 20) {
      return {};
    }

    CF_EXPECT(CreateBlankImage(instance_.new_metadata_image(),
                               instance_.blank_metadata_image_mb(), "none"),
              "Failed to create \"" << instance_.new_metadata_image()
                                    << "\" with size "
                                    << instance_.blank_metadata_image_mb());
    return {};
  }
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class InitializeAccessKregistryImage : public SetupFeature {
 public:
  INJECT(InitializeAccessKregistryImage(
      const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeAccessKregistryImage"; }
  bool Enabled() const override { return !instance_.protected_vm(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    auto access_kregistry = instance_.access_kregistry_path();
    if (FileExists(access_kregistry)) {
      return {};
    }
    CF_EXPECT(CreateBlankImage(access_kregistry, 2 /* mb */, "none"),
              "Failed to create \"" << access_kregistry << "\"");
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
};

class InitializeHwcomposerPmemImage : public SetupFeature {
 public:
  INJECT(InitializeHwcomposerPmemImage(
      const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeHwcomposerPmemImage"; }
  bool Enabled() const override {
    return instance_.hwcomposer() != kHwComposerNone &&
           !instance_.protected_vm();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (FileExists(instance_.hwcomposer_pmem_path())) {
      return {};
    }
    CF_EXPECT(
        CreateBlankImage(instance_.hwcomposer_pmem_path(), 2 /* mb */, "none"),
        "Failed creating \"" << instance_.hwcomposer_pmem_path() << "\"");
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
};

class InitializePstore : public SetupFeature {
 public:
  INJECT(InitializePstore(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializePstore"; }
  bool Enabled() const override { return !instance_.protected_vm(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (FileExists(instance_.pstore_path())) {
      return {};
    }

    CF_EXPECT(CreateBlankImage(instance_.pstore_path(), 2 /* mb */, "none"),
              "Failed to create \"" << instance_.pstore_path() << "\"");
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
};

class InitializeSdCard : public SetupFeature {
 public:
  INJECT(InitializeSdCard(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeSdCard"; }
  bool Enabled() const override {
    return instance_.use_sdcard() && !instance_.protected_vm();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (FileExists(instance_.sdcard_path())) {
      return {};
    }
    CF_EXPECT(CreateBlankImage(instance_.sdcard_path(),
                               instance_.blank_sdcard_image_mb(), "sdcard"),
              "Failed to create \"" << instance_.sdcard_path() << "\"");
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
};

class VbmetaEnforceMinimumSize : public SetupFeature {
 public:
  INJECT(VbmetaEnforceMinimumSize(
      const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  std::string Name() const override { return "VbmetaEnforceMinimumSize"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    // libavb expects to be able to read the maximum vbmeta size, so we must
    // provide a partition which matches this or the read will fail
    for (const auto& vbmeta_image :
         {instance_.vbmeta_image(), instance_.vbmeta_system_image(),
          instance_.vbmeta_vendor_dlkm_image(), instance_.vbmeta_system_dlkm_image()}) {
      // In some configurations of cuttlefish, the vendor dlkm vbmeta image does
      // not exist
      if (FileExists(vbmeta_image) && FileSize(vbmeta_image) != VBMETA_MAX_SIZE) {
        auto fd = SharedFD::Open(vbmeta_image, O_RDWR);
        CF_EXPECTF(fd->IsOpen(), "Could not open \"{}\": {}", vbmeta_image,
                   fd->StrError());
        CF_EXPECTF(fd->Truncate(VBMETA_MAX_SIZE) == 0,
                   "`truncate --size={} {}` failed: {}", VBMETA_MAX_SIZE,
                   vbmeta_image, fd->StrError());
      }
    }
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
};

class BootloaderPresentCheck : public SetupFeature {
 public:
  INJECT(BootloaderPresentCheck(
      const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  std::string Name() const override { return "BootloaderPresentCheck"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    CF_EXPECT(FileHasContent(instance_.bootloader()),
              "File not found: " << instance_.bootloader());
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
};

static fruit::Component<> DiskChangesComponent(
    const FetcherConfig* fetcher, const CuttlefishConfig* config,
    const CuttlefishConfig::InstanceSpecific* instance) {
  return fruit::createComponent()
      .bindInstance(*fetcher)
      .bindInstance(*config)
      .bindInstance(*instance)
      .addMultibinding<SetupFeature, InitializeMetadataImage>()
      .install(KernelRamdiskRepackerComponent)
      .addMultibinding<SetupFeature, VbmetaEnforceMinimumSize>()
      .addMultibinding<SetupFeature, BootloaderPresentCheck>()
      .install(Gem5ImageUnpackerComponent)
      .install(InitializeMiscImageComponent)
      // Create esp if necessary
      .install(InitializeEspImageComponent)
      .install(SuperImageRebuilderComponent);
}

static fruit::Component<> DiskChangesPerInstanceComponent(
    const FetcherConfig* fetcher, const CuttlefishConfig* config,
    const CuttlefishConfig::InstanceSpecific* instance) {
  return fruit::createComponent()
      .bindInstance(*fetcher)
      .bindInstance(*config)
      .bindInstance(*instance)
      .addMultibinding<SetupFeature, InitializeAccessKregistryImage>()
      .addMultibinding<SetupFeature, InitializeHwcomposerPmemImage>()
      .addMultibinding<SetupFeature, InitializePstore>()
      .addMultibinding<SetupFeature, InitializeSdCard>()
      .install(InitializeFactoryResetProtectedComponent)
      .install(GeneratePersistentBootconfigComponent)
      .install(GeneratePersistentVbmetaComponent)
      .install(InitializeInstanceCompositeDiskComponent)
      .install(InitializeDataImageComponent)
      .install(InitBootloaderEnvPartitionComponent);
}

Result<void> DiskImageFlagsVectorization(CuttlefishConfig& config, const FetcherConfig& fetcher_config) {
  std::vector<std::string> boot_image =
      android::base::Split(FLAGS_boot_image, ",");
  std::vector<std::string> init_boot_image =
      android::base::Split(FLAGS_init_boot_image, ",");
  std::vector<std::string> data_image =
      android::base::Split(FLAGS_data_image, ",");
  std::vector<std::string> super_image =
      android::base::Split(FLAGS_super_image, ",");
  std::vector<std::string> misc_image =
      android::base::Split(FLAGS_misc_image, ",");
  std::vector<std::string> misc_info =
      android::base::Split(FLAGS_misc_info_txt, ",");
  std::vector<std::string> metadata_image =
      android::base::Split(FLAGS_metadata_image, ",");
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

  std::vector<std::string> default_target_zip_vec =
      android::base::Split(FLAGS_default_target_zip, ",");
  std::vector<std::string> system_target_zip_vec =
      android::base::Split(FLAGS_system_target_zip, ",");

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

  std::vector<std::string> bootloader =
      android::base::Split(FLAGS_bootloader, ",");
  std::vector<std::string> initramfs_path =
      android::base::Split(FLAGS_initramfs_path, ",");
  std::vector<std::string> kernel_path =
      android::base::Split(FLAGS_kernel_path, ",");

  std::vector<std::string> blank_metadata_image_mb =
      android::base::Split(FLAGS_blank_metadata_image_mb, ",");
  std::vector<std::string> blank_sdcard_image_mb =
      android::base::Split(FLAGS_blank_sdcard_image_mb, ",");

  std::string cur_kernel_path;
  std::string cur_initramfs_path;
  std::string cur_boot_image;
  std::string cur_vendor_boot_image;
  std::string cur_super_image;
  std::string cur_metadata_image;
  std::string cur_misc_image;
  int cur_blank_metadata_image_mb{};
  int value{};
  int instance_index = 0;
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  for (const auto& num : instance_nums) {
    auto instance = config.ForInstance(num);
    if (instance_index >= misc_image.size()) {
      // legacy variable. Vectorize by copy [0] to all instances
      cur_misc_image = misc_image[0];
    } else {
      cur_misc_image = misc_image[instance_index];
    }
    instance.set_misc_image(cur_misc_image);
    if (instance_index >= misc_info.size()) {
      instance.set_misc_info_txt(misc_info[0]);
    } else {
      instance.set_misc_info_txt(misc_info[instance_index]);
    }
    if (instance_index >= boot_image.size()) {
      cur_boot_image = boot_image[0];
    } else {
      cur_boot_image = boot_image[instance_index];
    }
    instance.set_boot_image(cur_boot_image);
    instance.set_new_boot_image(cur_boot_image);

    if (instance_index >= init_boot_image.size()) {
      instance.set_init_boot_image(init_boot_image[0]);
    } else {
      instance.set_init_boot_image(init_boot_image[instance_index]);
    }
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
    if (instance_index >= super_image.size()) {
      cur_super_image = super_image[0];
    } else {
      cur_super_image = super_image[instance_index];
    }
    instance.set_super_image(cur_super_image);
    if (instance_index >= data_image.size()) {
      instance.set_data_image(data_image[0]);
    } else {
      instance.set_data_image(data_image[instance_index]);
    }
    if (instance_index >= metadata_image.size()) {
      cur_metadata_image = metadata_image[0];
    } else {
      cur_metadata_image = metadata_image[instance_index];
    }
    instance.set_metadata_image(cur_metadata_image);
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
      instance.set_fuchsia_multiboot_bin_path(fuchsia_multiboot_bin_path[instance_index]);
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
    if (instance_index >= bootloader.size()) {
      instance.set_bootloader(bootloader[0]);
    } else {
      instance.set_bootloader(bootloader[instance_index]);
    }
    if (instance_index >= kernel_path.size()) {
      cur_kernel_path = kernel_path[0];
    } else {
      cur_kernel_path = kernel_path[instance_index];
    }
    instance.set_kernel_path(cur_kernel_path);
    if (instance_index >= initramfs_path.size()) {
      cur_initramfs_path = initramfs_path[0];
    } else {
      cur_initramfs_path = initramfs_path[instance_index];
    }
    instance.set_initramfs_path(cur_initramfs_path);

    if (instance_index >= blank_metadata_image_mb.size()) {
      CHECK(android::base::ParseInt(blank_metadata_image_mb[0],
                                    &value))
          << "Invalid 'blank_metadata_image_mb' "
          << blank_metadata_image_mb[0];
    } else {
      CHECK(android::base::ParseInt(blank_metadata_image_mb[instance_index],
                                    &value))
          << "Invalid 'blank_metadata_image_mb' "
          << blank_metadata_image_mb[instance_index];
    }
    instance.set_blank_metadata_image_mb(value);
    cur_blank_metadata_image_mb = value;

    if (instance_index >= blank_sdcard_image_mb.size()) {
      CHECK(android::base::ParseInt(blank_sdcard_image_mb[0],
                                    &value))
          << "Invalid 'blank_sdcard_image_mb' "
          << blank_sdcard_image_mb[0];
    } else {
      CHECK(android::base::ParseInt(blank_sdcard_image_mb[instance_index],
                                    &value))
          << "Invalid 'blank_sdcard_image_mb' "
          << blank_sdcard_image_mb[instance_index];
    }
    instance.set_blank_sdcard_image_mb(value);

    // Repacking a boot.img changes boot_image and vendor_boot_image paths
    const CuttlefishConfig& const_config = const_cast<const CuttlefishConfig&>(config);
    const CuttlefishConfig::InstanceSpecific const_instance = const_config.ForInstance(num);
    if (cur_kernel_path.size() &&
        config.vm_manager() != Gem5Manager::name()) {
      const std::string new_boot_image_path =
          const_instance.PerInstancePath("boot_repacked.img");
      // change the new flag value to corresponding instance
      instance.set_new_boot_image(new_boot_image_path.c_str());
    }

    if (cur_kernel_path.size() || cur_initramfs_path.size()) {
      const std::string new_vendor_boot_image_path =
          const_instance.PerInstancePath("vendor_boot_repacked.img");
      // Repack the vendor boot images if kernels and/or ramdisks are passed in.
      if (cur_initramfs_path.size()) {
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
    if (CF_EXPECT(SuperImageNeedsRebuilding(fetcher_config,
                  const_instance.default_target_zip(),
                  const_instance.system_target_zip())) ||
        cur_initramfs_path.size()) {
      const std::string new_super_image_path =
          const_instance.PerInstancePath("super.img");
      instance.set_new_super_image(new_super_image_path);
    }

    if (FileExists(cur_metadata_image) &&
        FileSize(cur_metadata_image) == cur_blank_metadata_image_mb << 20) {
      instance.set_new_metadata_image(cur_metadata_image);
    } else {
      const std::string new_metadata_image_path =
          const_instance.PerInstancePath("metadata.img");
      instance.set_new_metadata_image(new_metadata_image_path);
    }
    instance.set_new_vbmeta_vendor_dlkm_image(
        const_instance.PerInstancePath("vbmeta_vendor_dlkm_repacked.img"));
    instance.set_new_vbmeta_system_dlkm_image(
        const_instance.PerInstancePath("vbmeta_system_dlkm_repacked.img"));

    if (FileHasContent(cur_misc_image)) {
      instance.set_new_misc_image(cur_misc_image);
    } else {
      const std::string new_misc_image_path =
          const_instance.PerInstancePath("misc.img");
      instance.set_new_misc_image(new_misc_image_path);
    }
    instance_index++;
  }
  return {};
}

Result<void> CreateDynamicDiskFiles(const FetcherConfig& fetcher_config,
                                    const CuttlefishConfig& config) {
  for (const auto& instance : config.Instances()) {
    // TODO(schuffelen): Unify this with the other injector created in
    // assemble_cvd.cpp
    fruit::Injector<> injector(DiskChangesComponent, &fetcher_config, &config,
                               &instance);
    for (auto& late_injected : injector.getMultibindings<LateInjected>()) {
      CF_EXPECT(late_injected->LateInject(injector));
    }

    const auto& features = injector.getMultibindings<SetupFeature>();
    CF_EXPECT(SetupFeature::RunSetup(features));
    fruit::Injector<> instance_injector(DiskChangesPerInstanceComponent,
                                        &fetcher_config, &config, &instance);
    for (auto& late_injected :
         instance_injector.getMultibindings<LateInjected>()) {
      CF_EXPECT(late_injected->LateInject(instance_injector));
    }

    const auto& instance_features =
        instance_injector.getMultibindings<SetupFeature>();
    CF_EXPECT(SetupFeature::RunSetup(instance_features),
              "instance = \"" << instance.instance_name() << "\"");

    // Check if filling in the sparse image would run out of disk space.
    auto existing_sizes = SparseFileSizes(instance.data_image());
    CF_EXPECT(existing_sizes.sparse_size > 0 || existing_sizes.disk_size > 0,
              "Unable to determine size of \"" << instance.data_image()
                                               << "\". Does this file exist?");
    auto available_space = AvailableSpaceAtPath(instance.data_image());
    if (available_space <
        existing_sizes.sparse_size - existing_sizes.disk_size) {
      // TODO(schuffelen): Duplicate this check in run_cvd when it can run on a
      // separate machine
      return CF_ERR("Not enough space remaining in fs containing \""
                    << instance.data_image() << "\", wanted "
                    << (existing_sizes.sparse_size - existing_sizes.disk_size)
                    << ", got " << available_space);
    } else {
      LOG(DEBUG) << "Available space: " << available_space;
      LOG(DEBUG) << "Sparse size of \"" << instance.data_image()
                 << "\": " << existing_sizes.sparse_size;
      LOG(DEBUG) << "Disk size of \"" << instance.data_image()
                 << "\": " << existing_sizes.disk_size;
    }

    auto os_disk_builder = OsCompositeDiskBuilder(config, instance);
    const auto os_built_composite = CF_EXPECT(os_disk_builder.BuildCompositeDiskIfNecessary());

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

    if (!instance.protected_vm()) {
      os_disk_builder.OverlayPath(instance.PerInstancePath("overlay.img"));
      CF_EXPECT(os_disk_builder.BuildOverlayIfNecessary());
      if (instance.ap_boot_flow() != APBootFlow::None) {
        ap_disk_builder.OverlayPath(instance.PerInstancePath("ap_overlay.img"));
        CF_EXPECT(ap_disk_builder.BuildOverlayIfNecessary());
      }
    }
  }

  for (auto instance : config.Instances()) {
    // Check that the files exist
    for (const auto& file : instance.virtual_disk_paths()) {
      if (!file.empty()) {
        CF_EXPECT(FileHasContent(file), "File not found: \"" << file << "\"");
      }
    }
    // Gem5 Simulate per-instance what the bootloader would usually do
    // Since on other devices this runs every time, just do it here every time
    if (config.vm_manager() == Gem5Manager::name()) {
      RepackGem5BootImage(instance.PerInstancePath("initrd.img"),
                          instance.persistent_bootconfig_path(),
                          config.assembly_dir(), instance.initramfs_path());
    }
  }

  return {};
}

} // namespace cuttlefish
