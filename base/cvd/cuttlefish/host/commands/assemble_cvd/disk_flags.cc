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

#include <sys/statvfs.h>

#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parsebool.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <gflags/gflags.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_bootconfig.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/commands/assemble_cvd/disk/access_kregistry.h"
#include "host/commands/assemble_cvd/disk/ap_composite_disk.h"
#include "host/commands/assemble_cvd/disk/bootloader_present.h"
#include "host/commands/assemble_cvd/disk/chromeos_state.h"
#include "host/commands/assemble_cvd/disk/factory_reset_protected.h"
#include "host/commands/assemble_cvd/disk/gem5_image_unpacker.h"
#include "host/commands/assemble_cvd/disk/generate_persistent_bootconfig.h"
#include "host/commands/assemble_cvd/disk/generate_persistent_vbmeta.h"
#include "host/commands/assemble_cvd/disk/hwcomposer_pmem.h"
#include "host/commands/assemble_cvd/disk/initialize_instance_composite_disk.h"
#include "host/commands/assemble_cvd/disk/kernel_ramdisk_repacker.h"
#include "host/commands/assemble_cvd/disk/metadata_image.h"
#include "host/commands/assemble_cvd/disk/os_composite_disk.h"
#include "host/commands/assemble_cvd/disk/pflash.h"
#include "host/commands/assemble_cvd/disk/pstore.h"
#include "host/commands/assemble_cvd/disk/sd_card.h"
#include "host/commands/assemble_cvd/disk/vbmeta_enforce_minimum_size.h"
#include "host/commands/assemble_cvd/disk_builder.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/assemble_cvd/super_image_mixer.h"
#include "host/libs/avb/avb.h"
#include "host/libs/config/ap_boot_flow.h"
#include "host/libs/config/boot_flow.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/data_image.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/config/instance_nums.h"
#include "host/libs/feature/inject.h"
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
DEFINE_string(misc_info_txt, "", "Location of the misc_info.txt file.");
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
DEFINE_string(default_vvmtruststore_file_name,
              CF_DEFAULTS_DEFAULT_VVMTRUSTSTORE_FILE_NAME,
              "If the vvmtruststore_path parameter is empty then the default "
              "file name of the vvmtruststore image in the directory specified"
              " by -system_image_dir. If empty then there's no vvmtruststore "
              "image assumed by default.");
DEFINE_string(vvmtruststore_path, CF_DEFAULTS_VVMTRUSTSTORE_PATH,
              "Location of the vvmtruststore image. If empty and the "
              "default_vvmtruststore_file_name parameter is not empty then the "
              "image file is assumed to be the default_vvmtruststore_file_name "
              "file in the directory specified by -system_image_dir.");

DEFINE_string(
    default_target_zip, CF_DEFAULTS_DEFAULT_TARGET_ZIP,
    "Location of default target zip file.");
DEFINE_string(
    system_target_zip, CF_DEFAULTS_SYSTEM_TARGET_ZIP,
    "Location of system target zip file.");

DEFINE_string(android_efi_loader, CF_DEFAULTS_ANDROID_EFI_LOADER,
              "Location of android EFI loader for android efi load flow.");

DEFINE_string(linux_kernel_path, CF_DEFAULTS_LINUX_KERNEL_PATH,
              "Location of linux kernel for cuttlefish otheros flow.");
DEFINE_string(linux_initramfs_path, CF_DEFAULTS_LINUX_INITRAMFS_PATH,
              "Location of linux initramfs.img for cuttlefish otheros flow.");
DEFINE_string(linux_root_image, CF_DEFAULTS_LINUX_ROOT_IMAGE,
              "Location of linux root filesystem image for cuttlefish otheros flow.");

DEFINE_string(chromeos_disk, CF_DEFAULTS_CHROMEOS_DISK,
              "Location of a complete ChromeOS GPT disk");
DEFINE_string(chromeos_kernel_path, CF_DEFAULTS_CHROMEOS_KERNEL_PATH,
              "Location of the chromeos kernel for the chromeos flow.");
DEFINE_string(chromeos_root_image, CF_DEFAULTS_CHROMEOS_ROOT_IMAGE,
              "Location of chromeos root filesystem image for chromeos flow.");

DEFINE_string(fuchsia_zedboot_path, CF_DEFAULTS_FUCHSIA_ZEDBOOT_PATH,
              "Location of fuchsia zedboot path for cuttlefish otheros flow.");
DEFINE_string(fuchsia_multiboot_bin_path, CF_DEFAULTS_FUCHSIA_MULTIBOOT_BIN_PATH,
              "Location of fuchsia multiboot bin path for cuttlefish otheros flow.");
DEFINE_string(fuchsia_root_image, CF_DEFAULTS_FUCHSIA_ROOT_IMAGE,
              "Location of fuchsia root filesystem image for cuttlefish otheros flow.");

DEFINE_string(
    custom_partition_path, CF_DEFAULTS_CUSTOM_PARTITION_PATH,
    "Location of custom image that will be passed as a \"custom\" partition"
    "to rootfs and can be used by /dev/block/by-name/custom. Multiple images "
    "can be passed, separated by semicolons and can be used as "
    "/dev/block/by-name/custom_1, /dev/block/by-name/custom_2, etc. Example: "
    "--custom_partition_path=\"/path/to/custom.img;/path/to/other.img\"");

DEFINE_string(
    hibernation_image, CF_DEFAULTS_HIBERNATION_IMAGE,
    "Location of the hibernation path that will be used when hibernating.");

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

  // It is conflict (invalid) to pass both kernel_path/initramfs_path
  // and image file paths.
  bool flags_kernel_initramfs_has_input = (!FLAGS_kernel_path.empty())
                                          || (!FLAGS_initramfs_path.empty());
  bool flags_image_has_input =
      (!FLAGS_super_image.empty()) || (!FLAGS_vendor_boot_image.empty()) ||
      (!FLAGS_vbmeta_vendor_dlkm_image.empty()) ||
      (!FLAGS_vbmeta_system_dlkm_image.empty()) || (!FLAGS_boot_image.empty());
  CF_EXPECT(!(flags_kernel_initramfs_has_input && flags_image_has_input),
             "Cannot pass both kernel_path/initramfs_path and image file paths");

  std::vector<std::string> system_image_dir =
      android::base::Split(FLAGS_system_image_dir, ",");
  std::string default_boot_image = "";
  std::string default_init_boot_image = "";
  std::string default_data_image = "";
  std::string default_super_image = "";
  std::string default_misc_info_txt = "";
  std::string default_vendor_boot_image = "";
  std::string default_vbmeta_image = "";
  std::string default_vbmeta_system_image = "";
  std::string default_vbmeta_vendor_dlkm_image = "";
  std::string default_vbmeta_system_dlkm_image = "";
  std::string default_16k_kernel_image = "";
  std::string default_16k_ramdisk_image = "";
  std::string default_hibernation_image = "";
  std::string vvmtruststore_path = "";

  std::string cur_system_image_dir;
  std::string comma_str = "";
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  auto default_vvmtruststore_file_name =
      android::base::Split(FLAGS_default_vvmtruststore_file_name, ",");
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
    default_super_image += comma_str + cur_system_image_dir + "/super.img";
    default_misc_info_txt +=
        comma_str + cur_system_image_dir + "/misc_info.txt";
    default_vendor_boot_image += comma_str + cur_system_image_dir + "/vendor_boot.img";
    default_vbmeta_image += comma_str + cur_system_image_dir + "/vbmeta.img";
    default_vbmeta_system_image += comma_str + cur_system_image_dir + "/vbmeta_system.img";
    default_vbmeta_vendor_dlkm_image +=
        comma_str + cur_system_image_dir + "/vbmeta_vendor_dlkm.img";
    default_vbmeta_system_dlkm_image +=
        comma_str + cur_system_image_dir + "/vbmeta_system_dlkm.img";
    default_hibernation_image +=
        comma_str + cur_system_image_dir + "/hibernation_swap.img";
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

    if (instance_index < default_vvmtruststore_file_name.size()) {
      if (default_vvmtruststore_file_name[instance_index].empty()) {
        vvmtruststore_path += comma_str;
      } else {
        vvmtruststore_path += comma_str + cur_system_image_dir + "/" +
                              default_vvmtruststore_file_name[instance_index];
      }
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
  SetCommandLineOptionWithMode("super_image", default_super_image.c_str(),
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
  SetCommandLineOptionWithMode("hibernation_image",
                               default_hibernation_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("vvmtruststore_path", vvmtruststore_path.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  return {};
}

DiskBuilder OsCompositeDiskBuilder(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance,
    const MetadataImage& metadata) {
  auto builder =
      DiskBuilder()
          .VmManager(config.vm_manager())
          .CrosvmPath(instance.crosvm_binary())
          .ConfigPath(instance.PerInstancePath("os_composite_disk_config.txt"))
          .ReadOnly(FLAGS_use_overlay)
          .ResumeIfPossible(FLAGS_resume);
  if (instance.boot_flow() == BootFlow::ChromeOsDisk) {
    return builder.EntireDisk(instance.chromeos_disk())
        .CompositeDiskPath(instance.chromeos_disk());
  }
  return builder.Partitions(GetOsCompositeDiskConfig(instance, metadata))
      .HeaderPath(instance.PerInstancePath("os_composite_gpt_header.img"))
      .FooterPath(instance.PerInstancePath("os_composite_gpt_footer.img"))
      .CompositeDiskPath(instance.os_composite_disk_path());
}

DiskBuilder ApCompositeDiskBuilder(const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  return DiskBuilder()
      .ReadOnly(FLAGS_use_overlay)
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

static fruit::Component<> DiskChangesComponent(
    const FetcherConfig* fetcher, const CuttlefishConfig* config,
    const CuttlefishConfig::InstanceSpecific* instance) {
  return fruit::createComponent()
      .bindInstance(*fetcher)
      .bindInstance(*config)
      .bindInstance(*instance)
      .install(CuttlefishKeyAvbComponent)
      .install(AutoSetup<InitializeChromeOsState>::Component)
      .install(AutoSetup<RepackKernelRamdisk>::Component)
      .install(AutoSetup<VbmetaEnforceMinimumSize>::Component)
      .install(AutoSetup<BootloaderPresentCheck>::Component)
      .install(AutoSetup<Gem5ImageUnpacker>::Component)
      .install(AutoSetup<InitializeMiscImage>::Component)
      // Create esp if necessary
      .install(AutoSetup<InitializeEspImage>::Component)
      .install(SuperImageRebuilderComponent);
}

static fruit::Component<> DiskChangesPerInstanceComponent(
    const FetcherConfig* fetcher, const CuttlefishConfig* config,
    const CuttlefishConfig::InstanceSpecific* instance) {
  return fruit::createComponent()
      .bindInstance(*fetcher)
      .bindInstance(*config)
      .bindInstance(*instance)
      .install(AutoSetup<InitializeAccessKregistryImage>::Component)
      .install(AutoSetup<InitBootloaderEnvPartition>::Component)
      .install(AutoSetup<InitializeFactoryResetProtected>::Component)
      .install(AutoSetup<InitializeHwcomposerPmemImage>::Component)
      .install(AutoSetup<InitializePstore>::Component)
      .install(AutoSetup<InitializeSdCard>::Component)
      .install(AutoSetup<BootConfigPartition::CreateIfNeeded>::Component)
      .install(AutoSetup<GeneratePersistentVbmeta>::Component)
      .install(AutoSetup<InitializeInstanceCompositeDisk>::Component)
      .install(AutoSetup<InitializeDataImage>::Component)
      .install(AutoSetup<InitializePflash>::Component)
      .addMultibinding<AutoSetup<BootConfigPartition::CreateIfNeeded>::Type,
                       AutoSetup<BootConfigPartition::CreateIfNeeded>::Type>();
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
  std::vector<std::string> misc_info =
      android::base::Split(FLAGS_misc_info_txt, ",");
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

  std::vector<std::string> android_efi_loader =
      android::base::Split(FLAGS_android_efi_loader, ",");

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
  std::vector<std::string> hibernation_image =
      android::base::Split(FLAGS_hibernation_image, ",");

  std::vector<std::string> bootloader =
      android::base::Split(FLAGS_bootloader, ",");
  std::vector<std::string> initramfs_path =
      android::base::Split(FLAGS_initramfs_path, ",");
  std::vector<std::string> kernel_path =
      android::base::Split(FLAGS_kernel_path, ",");

  std::vector<std::string> blank_sdcard_image_mb =
      android::base::Split(FLAGS_blank_sdcard_image_mb, ",");

  std::string cur_kernel_path;
  std::string cur_initramfs_path;
  std::string cur_boot_image;
  std::string cur_vendor_boot_image;
  std::string cur_super_image;
  int value{};
  int instance_index = 0;
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  for (const auto& num : instance_nums) {
    auto instance = config.ForInstance(num);
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
    if (instance_index >= data_image.size()) {
      instance.set_data_image(data_image[0]);
    } else {
      instance.set_data_image(data_image[instance_index]);
    }
    if (instance_index >= android_efi_loader.size()) {
      instance.set_android_efi_loader(android_efi_loader[0]);
    } else {
      instance.set_android_efi_loader(android_efi_loader[instance_index]);
    }
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
    if (instance_index >= hibernation_image.size()) {
      instance.set_hibernation_partition_image(hibernation_image[0]);
    } else {
      instance.set_hibernation_partition_image(
          hibernation_image[instance_index]);
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
    const CuttlefishConfig& const_config = const_cast<const CuttlefishConfig&>(config);
    const CuttlefishConfig::InstanceSpecific const_instance = const_config.ForInstance(num);
    if (!cur_kernel_path.empty() && config.vm_manager() != VmmMode::kGem5) {
      const std::string new_boot_image_path =
          const_instance.PerInstancePath("boot_repacked.img");
      // change the new flag value to corresponding instance
      instance.set_new_boot_image(new_boot_image_path.c_str());
    }

    instance.set_new_data_image(const_instance.PerInstancePath("userdata.img"));
    if (instance_index >= data_image.size()) {
      instance.set_data_image(data_image[0]);
    } else {
      instance.set_data_image(data_image[instance_index]);
    }

    if (!cur_kernel_path.empty() || !cur_initramfs_path.empty()) {
      const std::string new_vendor_boot_image_path =
          const_instance.PerInstancePath("vendor_boot_repacked.img");
      // Repack the vendor boot images if kernels and/or ramdisks are passed in.
      if (!cur_initramfs_path.empty()) {
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
        !cur_initramfs_path.empty()) {
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

    DiskBuilder os_disk_builder =
        OsCompositeDiskBuilder(config, instance, metadata);
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

    std::vector<AutoSetup<BootConfigPartition::CreateIfNeeded>::Type*>
        bootconfig_binding = instance_injector.getMultibindings<
            AutoSetup<BootConfigPartition::CreateIfNeeded>::Type>();
    CF_EXPECT(!bootconfig_binding.empty());

    const std::optional<BootConfigPartition>& bootconfig_partition =
        **(bootconfig_binding[0]);
    // Gem5 Simulate per-instance what the bootloader would usually do
    // Since on other devices this runs every time, just do it here every time
    if (config.vm_manager() == VmmMode::kGem5) {
      RepackGem5BootImage(instance.PerInstancePath("initrd.img"),
                          bootconfig_partition, config.assembly_dir(),
                          instance.initramfs_path());
    }
  }

  return {};
}

} // namespace cuttlefish
