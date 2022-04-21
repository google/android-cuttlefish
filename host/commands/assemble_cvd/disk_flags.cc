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
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <gflags/gflags.h>
#include <sys/statvfs.h>

#include <fstream>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/size_utils.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/commands/assemble_cvd/disk_builder.h"
#include "host/commands/assemble_cvd/super_image_mixer.h"
#include "host/libs/config/bootconfig_args.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/data_image.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/gem5_manager.h"

// Taken from external/avb/libavb/avb_slot_verify.c; this define is not in the headers
#define VBMETA_MAX_SIZE 65536ul
// Taken from external/avb/avbtool.py; this define is not in the headers
#define MAX_AVB_METADATA_SIZE 69632ul

DECLARE_string(system_image_dir);

DEFINE_string(boot_image, "",
              "Location of cuttlefish boot image. If empty it is assumed to be "
              "boot.img in the directory specified by -system_image_dir.");
DEFINE_string(
    init_boot_image, "",
    "Location of cuttlefish init boot image. If empty it is assumed to "
    "be init_boot.img in the directory specified by -system_image_dir.");
DEFINE_string(data_image, "", "Location of the data partition image.");
DEFINE_string(super_image, "", "Location of the super partition image.");
DEFINE_string(misc_image, "",
              "Location of the misc partition image. If the image does not "
              "exist, a blank new misc partition image is created.");
DEFINE_string(metadata_image, "", "Location of the metadata partition image "
              "to be generated.");
DEFINE_string(vendor_boot_image, "",
              "Location of cuttlefish vendor boot image. If empty it is assumed to "
              "be vendor_boot.img in the directory specified by -system_image_dir.");
DEFINE_string(vbmeta_image, "",
              "Location of cuttlefish vbmeta image. If empty it is assumed to "
              "be vbmeta.img in the directory specified by -system_image_dir.");
DEFINE_string(vbmeta_system_image, "",
              "Location of cuttlefish vbmeta_system image. If empty it is assumed to "
              "be vbmeta_system.img in the directory specified by -system_image_dir.");
DEFINE_string(otheros_esp_image, "",
              "Location of cuttlefish esp image. If the image does not exist, "
              "and --otheros_root_image is specified, an esp partition image "
              "is created with default bootloaders.");
DEFINE_string(otheros_kernel_path, "",
              "Location of cuttlefish otheros kernel.");
DEFINE_string(otheros_initramfs_path, "",
              "Location of cuttlefish otheros initramfs.img.");
DEFINE_string(otheros_root_image, "",
              "Location of cuttlefish otheros root filesystem image.");

DEFINE_int32(blank_metadata_image_mb, 16,
             "The size of the blank metadata image to generate, MB.");
DEFINE_int32(blank_sdcard_image_mb, 2048,
             "If enabled, the size of the blank sdcard image to generate, MB.");

DECLARE_string(ap_rootfs_image);
DECLARE_string(bootloader);
DECLARE_bool(use_sdcard);
DECLARE_string(initramfs_path);
DECLARE_string(kernel_path);
DECLARE_bool(resume);
DECLARE_bool(protected_vm);

namespace cuttlefish {

using vm_manager::Gem5Manager;

Result<void> ResolveInstanceFiles() {
  CF_EXPECT(!FLAGS_system_image_dir.empty(),
            "--system_image_dir must be specified.");

  // If user did not specify location of either of these files, expect them to
  // be placed in --system_image_dir location.
  std::string default_boot_image = FLAGS_system_image_dir + "/boot.img";
  SetCommandLineOptionWithMode("boot_image", default_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_init_boot_image =
      FLAGS_system_image_dir + "/init_boot.img";
  SetCommandLineOptionWithMode("init_boot_image",
                               default_init_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_data_image = FLAGS_system_image_dir + "/userdata.img";
  SetCommandLineOptionWithMode("data_image", default_data_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_metadata_image = FLAGS_system_image_dir + "/metadata.img";
  SetCommandLineOptionWithMode("metadata_image", default_metadata_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_super_image = FLAGS_system_image_dir + "/super.img";
  SetCommandLineOptionWithMode("super_image", default_super_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_misc_image = FLAGS_system_image_dir + "/misc.img";
  SetCommandLineOptionWithMode("misc_image", default_misc_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_esp_image = FLAGS_system_image_dir + "/esp.img";
  SetCommandLineOptionWithMode("otheros_esp_image", default_esp_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_vendor_boot_image = FLAGS_system_image_dir
                                        + "/vendor_boot.img";
  SetCommandLineOptionWithMode("vendor_boot_image",
                               default_vendor_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_vbmeta_image = FLAGS_system_image_dir + "/vbmeta.img";
  SetCommandLineOptionWithMode("vbmeta_image", default_vbmeta_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_vbmeta_system_image = FLAGS_system_image_dir
                                          + "/vbmeta_system.img";
  SetCommandLineOptionWithMode("vbmeta_system_image",
                               default_vbmeta_system_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  return {};
}

std::vector<ImagePartition> GetOsCompositeDiskConfig() {
  std::vector<ImagePartition> partitions;
  partitions.push_back(ImagePartition{
      .label = "misc",
      .image_file_path = AbsolutePath(FLAGS_misc_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "boot_a",
      .image_file_path = AbsolutePath(FLAGS_boot_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "boot_b",
      .image_file_path = AbsolutePath(FLAGS_boot_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "init_boot_a",
      .image_file_path = AbsolutePath(FLAGS_init_boot_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "init_boot_b",
      .image_file_path = AbsolutePath(FLAGS_init_boot_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "vendor_boot_a",
      .image_file_path = AbsolutePath(FLAGS_vendor_boot_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "vendor_boot_b",
      .image_file_path = AbsolutePath(FLAGS_vendor_boot_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_a",
      .image_file_path = AbsolutePath(FLAGS_vbmeta_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_b",
      .image_file_path = AbsolutePath(FLAGS_vbmeta_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_system_a",
      .image_file_path = AbsolutePath(FLAGS_vbmeta_system_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta_system_b",
      .image_file_path = AbsolutePath(FLAGS_vbmeta_system_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "super",
      .image_file_path = AbsolutePath(FLAGS_super_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "userdata",
      .image_file_path = AbsolutePath(FLAGS_data_image),
      .read_only = true,
  });
  partitions.push_back(ImagePartition{
      .label = "metadata",
      .image_file_path = AbsolutePath(FLAGS_metadata_image),
      .read_only = true,
  });
  if (!FLAGS_otheros_root_image.empty()) {
    partitions.push_back(ImagePartition{
        .label = "otheros_esp",
        .image_file_path = AbsolutePath(FLAGS_otheros_esp_image),
        .type = kEfiSystemPartition,
        .read_only = true,
    });
    partitions.push_back(ImagePartition{
        .label = "otheros_root",
        .image_file_path = AbsolutePath(FLAGS_otheros_root_image),
        .read_only = true,
    });
  }
  if (!FLAGS_ap_rootfs_image.empty()) {
    partitions.push_back(ImagePartition{
        .label = "ap_rootfs",
        .image_file_path = AbsolutePath(FLAGS_ap_rootfs_image),
        .read_only = true,
    });
  }
  return partitions;
}

DiskBuilder OsCompositeDiskBuilder(const CuttlefishConfig& config) {
  return DiskBuilder()
      .Partitions(GetOsCompositeDiskConfig())
      .VmManager(config.vm_manager())
      .CrosvmPath(config.crosvm_binary())
      .ConfigPath(config.AssemblyPath("os_composite_disk_config.txt"))
      .HeaderPath(config.AssemblyPath("os_composite_gpt_header.img"))
      .FooterPath(config.AssemblyPath("os_composite_gpt_footer.img"))
      .CompositeDiskPath(config.os_composite_disk_path())
      .ResumeIfPossible(FLAGS_resume);
}

std::vector<ImagePartition> persistent_composite_disk_config(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<ImagePartition> partitions;

  // Note that if the position of uboot_env changes, the environment for
  // u-boot must be updated as well (see boot_config.cc and
  // cuttlefish.fragment in external/u-boot).
  partitions.push_back(ImagePartition{
      .label = "uboot_env",
      .image_file_path = AbsolutePath(instance.uboot_env_image_path()),
  });
  partitions.push_back(ImagePartition{
      .label = "vbmeta",
      .image_file_path = AbsolutePath(instance.vbmeta_path()),
  });
  if (!FLAGS_protected_vm) {
    partitions.push_back(ImagePartition{
        .label = "frp",
        .image_file_path =
            AbsolutePath(instance.factory_reset_protected_path()),
    });
  }
  if (config.bootconfig_supported()) {
    partitions.push_back(ImagePartition{
        .label = "bootconfig",
        .image_file_path = AbsolutePath(instance.persistent_bootconfig_path()),
    });
  }
  return partitions;
}

static uint64_t AvailableSpaceAtPath(const std::string& path) {
  struct statvfs vfs;
  if (statvfs(path.c_str(), &vfs) != 0) {
    int error_num = errno;
    LOG(ERROR) << "Could not find space available at " << path << ", error was "
               << strerror(error_num);
    return 0;
  }
  // f_frsize (block size) * f_bavail (free blocks) for unprivileged users.
  return static_cast<uint64_t>(vfs.f_frsize) * vfs.f_bavail;
}

class BootImageRepacker : public SetupFeature {
 public:
  INJECT(BootImageRepacker(const CuttlefishConfig& config)) : config_(config) {}

  // SetupFeature
  std::string Name() const override { return "BootImageRepacker"; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Enabled() const override {
    // If we are booting a protected VM, for now, assume that image repacking
    // isn't trusted. Repacking requires resigning the image and keys from an
    // android host aren't trusted.
    return !config_.protected_vm();
  }

 protected:
  bool Setup() override {
    if (!FileHasContent(FLAGS_boot_image)) {
      LOG(ERROR) << "File not found: " << FLAGS_boot_image;
      return false;
    }
    // The init_boot partition is be optional for testing boot.img
    // with the ramdisk inside.
    if (!FileHasContent(FLAGS_init_boot_image)) {
      LOG(WARNING) << "File not found: " << FLAGS_init_boot_image;
    }

    if (!FileHasContent(FLAGS_vendor_boot_image)) {
      LOG(ERROR) << "File not found: " << FLAGS_vendor_boot_image;
      return false;
    }

    // Repacking a boot.img doesn't work with Gem5 because the user must always
    // specify a vmlinux instead of an arm64 Image, and that file can be too
    // large to be repacked. Skip repack of boot.img on Gem5, as we need to be
    // able to extract the ramdisk.img in a later stage and so this step must
    // not fail (..and the repacked kernel wouldn't be used anyway).
    if (FLAGS_kernel_path.size() &&
        config_.vm_manager() != Gem5Manager::name()) {
      const std::string new_boot_image_path =
          config_.AssemblyPath("boot_repacked.img");
      bool success =
          RepackBootImage(FLAGS_kernel_path, FLAGS_boot_image,
                          new_boot_image_path, config_.assembly_dir());
      if (!success) {
        LOG(ERROR) << "Failed to regenerate the boot image with the new kernel";
        return false;
      }
      SetCommandLineOptionWithMode("boot_image", new_boot_image_path.c_str(),
                                   google::FlagSettingMode::SET_FLAGS_DEFAULT);
    }

    if (FLAGS_kernel_path.size() || FLAGS_initramfs_path.size()) {
      const std::string new_vendor_boot_image_path =
          config_.AssemblyPath("vendor_boot_repacked.img");
      // Repack the vendor boot images if kernels and/or ramdisks are passed in.
      if (FLAGS_initramfs_path.size()) {
        bool success = RepackVendorBootImage(
            FLAGS_initramfs_path, FLAGS_vendor_boot_image,
            new_vendor_boot_image_path, config_.assembly_dir(),
            config_.bootconfig_supported());
        if (!success) {
          LOG(ERROR) << "Failed to regenerate the vendor boot image with the "
                        "new ramdisk";
        } else {
          // This control flow implies a kernel with all configs built in.
          // If it's just the kernel, repack the vendor boot image without a
          // ramdisk.
          bool success = RepackVendorBootImageWithEmptyRamdisk(
              FLAGS_vendor_boot_image, new_vendor_boot_image_path,
              config_.assembly_dir(), config_.bootconfig_supported());
          if (!success) {
            LOG(ERROR) << "Failed to regenerate the vendor boot image without "
                          "a ramdisk";
            return false;
          }
        }
        SetCommandLineOptionWithMode(
            "vendor_boot_image", new_vendor_boot_image_path.c_str(),
            google::FlagSettingMode::SET_FLAGS_DEFAULT);
      }
    }
    return true;
  }

 private:
  const CuttlefishConfig& config_;
};

class Gem5ImageUnpacker : public SetupFeature {
 public:
  INJECT(Gem5ImageUnpacker(
      const CuttlefishConfig& config,
      BootImageRepacker& bir))
      : config_(config),
        bir_(bir) {}

  // SetupFeature
  std::string Name() const override { return "Gem5ImageUnpacker"; }

  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {
        static_cast<SetupFeature*>(&bir_),
    };
  }

  bool Enabled() const override {
    // Everything has a bootloader except gem5, so only run this for gem5
    return config_.vm_manager() == Gem5Manager::name();
  }

 protected:
  bool Setup() override {
    /* Unpack the original or repacked boot and vendor boot ramdisks, so that
     * we have access to the baked bootconfig and raw compressed ramdisks.
     * This allows us to emulate what a bootloader would normally do, which
     * Gem5 can't support itself. This code also copies the kernel again
     * (because Gem5 only supports raw vmlinux) and handles the bootloader
     * binaries specially. This code is just part of the solution; it only
     * does the parts which are instance agnostic.
     */

    if (!FileHasContent(FLAGS_boot_image)) {
      LOG(ERROR) << "File not found: " << FLAGS_boot_image;
      return false;
    }
    // The init_boot partition is be optional for testing boot.img
    // with the ramdisk inside.
    if (!FileHasContent(FLAGS_init_boot_image)) {
      LOG(WARNING) << "File not found: " << FLAGS_init_boot_image;
    }

    if (!FileHasContent(FLAGS_vendor_boot_image)) {
      LOG(ERROR) << "File not found: " << FLAGS_vendor_boot_image;
      return false;
    }

    const std::string unpack_dir = config_.assembly_dir();

    bool success = UnpackBootImage(FLAGS_init_boot_image, unpack_dir);
    if (!success) {
      LOG(ERROR) << "Failed to extract the init boot image";
      return false;
    }

    success = UnpackVendorBootImageIfNotUnpacked(FLAGS_vendor_boot_image,
                                                 unpack_dir);
    if (!success) {
      LOG(ERROR) << "Failed to extract the vendor boot image";
      return false;
    }

    // Assume the user specified a kernel manually which is a vmlinux
    std::ofstream kernel(unpack_dir + "/kernel", std::ios_base::binary |
                                                 std::ios_base::trunc);
    std::ifstream vmlinux(FLAGS_kernel_path, std::ios_base::binary);
    kernel << vmlinux.rdbuf();
    kernel.close();

    // Gem5 needs the bootloader binary to be a specific directory structure
    // to find it. Create a 'binaries' directory and copy it into there
    const std::string binaries_dir = unpack_dir + "/binaries";
    if (mkdir(binaries_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0
        && errno != EEXIST) {
      PLOG(ERROR) << "Failed to create dir: \"" << binaries_dir << "\" ";
      return false;
    }
    std::ofstream bootloader(binaries_dir + "/" +
                             cpp_basename(FLAGS_bootloader),
                             std::ios_base::binary | std::ios_base::trunc);
    std::ifstream src_bootloader(FLAGS_bootloader, std::ios_base::binary);
    bootloader << src_bootloader.rdbuf();
    bootloader.close();

    // Gem5 also needs the ARM version of the bootloader, even though it
    // doesn't use it. It'll even open it to check it's a valid ELF file.
    // Work around this by copying such a named file from the same directory
    std::ofstream boot_arm(binaries_dir + "/boot.arm",
                           std::ios_base::binary | std::ios_base::trunc);
    std::ifstream src_boot_arm(cpp_dirname(FLAGS_bootloader) + "/boot.arm",
                               std::ios_base::binary);
    boot_arm << src_boot_arm.rdbuf();
    boot_arm.close();

    return true;
  }

 private:
  const CuttlefishConfig& config_;
  BootImageRepacker& bir_;
};

class GeneratePersistentBootconfig : public SetupFeature {
 public:
  INJECT(GeneratePersistentBootconfig(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // SetupFeature
  std::string Name() const override {
    return "GeneratePersistentBootconfig";
  }
  bool Enabled() const override {
    return (!config_.protected_vm());
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override {
    //  Cuttlefish for the time being won't be able to support OTA from a
    //  non-bootconfig kernel to a bootconfig-kernel (or vice versa) IF the
    //  device is stopped (via stop_cvd). This is rarely an issue since OTA
    //  testing run on cuttlefish is done within one launch cycle of the device.
    //  If this ever becomes an issue, this code will have to be rewritten.
    if(!config_.bootconfig_supported()) {
      return true;
    }

    const auto bootconfig_path = instance_.persistent_bootconfig_path();
    if (!FileExists(bootconfig_path)) {
      if (!CreateBlankImage(bootconfig_path, 1 /* mb */, "none")) {
        LOG(ERROR) << "Failed to create image at " << bootconfig_path;
        return false;
      }
    }

    auto bootconfig_fd = SharedFD::Open(bootconfig_path, O_RDWR);
    if (!bootconfig_fd->IsOpen()) {
      LOG(ERROR) << "Unable to open bootconfig file: "
                 << bootconfig_fd->StrError();
      return false;
    }

    const std::string bootconfig =
        android::base::Join(BootconfigArgsFromConfig(config_, instance_),
                            "\n") +
        "\n";
    ssize_t bytesWritten = WriteAll(bootconfig_fd, bootconfig);
    LOG(DEBUG) << "bootconfig size is " << bytesWritten;
    if (bytesWritten != bootconfig.size()) {
      LOG(ERROR) << "Failed to write contents of bootconfig to \""
                 << bootconfig_path << "\"";
      return false;
    }
    LOG(DEBUG) << "Bootconfig parameters from vendor boot image and config are "
               << ReadFile(bootconfig_path);

    if (bootconfig_fd->Truncate(bytesWritten) != 0) {
      LOG(ERROR) << "`truncate --size=" << bytesWritten << " bytes "
                 << bootconfig_path << "` failed:" << bootconfig_fd->StrError();
      return false;
    }

    if (config_.vm_manager() != Gem5Manager::name()) {
      bootconfig_fd->Close();
      const off_t bootconfig_size_bytes = AlignToPowerOf2(
          MAX_AVB_METADATA_SIZE + bytesWritten, PARTITION_SIZE_SHIFT);

      auto avbtool_path = HostBinaryPath("avbtool");
      Command bootconfig_hash_footer_cmd(avbtool_path);
      bootconfig_hash_footer_cmd.AddParameter("add_hash_footer");
      bootconfig_hash_footer_cmd.AddParameter("--image");
      bootconfig_hash_footer_cmd.AddParameter(bootconfig_path);
      bootconfig_hash_footer_cmd.AddParameter("--partition_size");
      bootconfig_hash_footer_cmd.AddParameter(bootconfig_size_bytes);
      bootconfig_hash_footer_cmd.AddParameter("--partition_name");
      bootconfig_hash_footer_cmd.AddParameter("bootconfig");
      bootconfig_hash_footer_cmd.AddParameter("--key");
      bootconfig_hash_footer_cmd.AddParameter(
          DefaultHostArtifactsPath("etc/cvd_avb_testkey.pem"));
      bootconfig_hash_footer_cmd.AddParameter("--algorithm");
      bootconfig_hash_footer_cmd.AddParameter("SHA256_RSA4096");
      int success = bootconfig_hash_footer_cmd.Start().Wait();
      if (success != 0) {
        LOG(ERROR) << "Unable to run append hash footer. Exited with status "
                   << success;
        return false;
      }
    } else {
      const off_t bootconfig_size_bytes_gem5 = AlignToPowerOf2(
          bytesWritten, PARTITION_SIZE_SHIFT);
      bootconfig_fd->Truncate(bootconfig_size_bytes_gem5);
      bootconfig_fd->Close();
    }
    return true;
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class GeneratePersistentVbmeta : public SetupFeature {
 public:
  INJECT(GeneratePersistentVbmeta(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance,
      InitBootloaderEnvPartition& bootloader_env,
      GeneratePersistentBootconfig& bootconfig))
      : config_(config),
        instance_(instance),
        bootloader_env_(bootloader_env),
        bootconfig_(bootconfig) {}

  // SetupFeature
  std::string Name() const override {
    return "GeneratePersistentVbmeta";
  }
  bool Enabled() const override {
    return (!config_.protected_vm());
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {
        static_cast<SetupFeature*>(&bootloader_env_),
        static_cast<SetupFeature*>(&bootconfig_),
    };
  }

  bool Setup() override {
    auto avbtool_path = HostBinaryPath("avbtool");
    Command vbmeta_cmd(avbtool_path);
    vbmeta_cmd.AddParameter("make_vbmeta_image");
    vbmeta_cmd.AddParameter("--output");
    vbmeta_cmd.AddParameter(instance_.vbmeta_path());
    vbmeta_cmd.AddParameter("--algorithm");
    vbmeta_cmd.AddParameter("SHA256_RSA4096");
    vbmeta_cmd.AddParameter("--key");
    vbmeta_cmd.AddParameter(
        DefaultHostArtifactsPath("etc/cvd_avb_testkey.pem"));

    vbmeta_cmd.AddParameter("--chain_partition");
    vbmeta_cmd.AddParameter("uboot_env:1:" +
                            DefaultHostArtifactsPath("etc/cvd.avbpubkey"));

    if (config_.bootconfig_supported()) {
        vbmeta_cmd.AddParameter("--chain_partition");
        vbmeta_cmd.AddParameter("bootconfig:2:" +
                                DefaultHostArtifactsPath("etc/cvd.avbpubkey"));
    }

    bool success = vbmeta_cmd.Start().Wait();
    if (success != 0) {
      LOG(ERROR) << "Unable to create persistent vbmeta. Exited with status "
                 << success;
      return false;
    }

    if (FileSize(instance_.vbmeta_path()) > VBMETA_MAX_SIZE) {
      LOG(ERROR) << "Generated vbmeta - " << instance_.vbmeta_path()
                 << " is larger than the expected " << VBMETA_MAX_SIZE
                 << ". Stopping.";
      return false;
    }
    if (FileSize(instance_.vbmeta_path()) != VBMETA_MAX_SIZE) {
      auto fd = SharedFD::Open(instance_.vbmeta_path(), O_RDWR);
      if (!fd->IsOpen() || fd->Truncate(VBMETA_MAX_SIZE) != 0) {
        LOG(ERROR) << "`truncate --size=" << VBMETA_MAX_SIZE << " "
                   << instance_.vbmeta_path() << "` "
                   << "failed: " << fd->StrError();
        return false;
      }
    }
    return true;
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  InitBootloaderEnvPartition& bootloader_env_;
  GeneratePersistentBootconfig& bootconfig_;
};

class InitializeMetadataImage : public SetupFeature {
 public:
  INJECT(InitializeMetadataImage()) {}

  // SetupFeature
  std::string Name() const override { return "InitializeMetadataImage"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (FileExists(FLAGS_metadata_image)) {
      return {};
    }

    CF_EXPECT(CreateBlankImage(FLAGS_metadata_image,
                               FLAGS_blank_metadata_image_mb, "none"),
              "Failed to create \"" << FLAGS_metadata_image << "\" with size "
                                    << FLAGS_blank_metadata_image_mb);
    return {};
  }
};

class InitializeAccessKregistryImage : public SetupFeature {
 public:
  INJECT(InitializeAccessKregistryImage(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeAccessKregistryImage"; }
  bool Enabled() const override { return !config_.protected_vm(); }

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

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class InitializeHwcomposerPmemImage : public SetupFeature {
 public:
  INJECT(InitializeHwcomposerPmemImage(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeHwcomposerPmemImage"; }
  bool Enabled() const override { return !config_.protected_vm(); }

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

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class InitializePstore : public SetupFeature {
 public:
  INJECT(InitializePstore(const CuttlefishConfig& config,
                          const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializePstore"; }
  bool Enabled() const override { return !config_.protected_vm(); }

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

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class InitializeSdCard : public SetupFeature {
 public:
  INJECT(InitializeSdCard(const CuttlefishConfig& config,
                          const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeSdCard"; }
  bool Enabled() const override {
    return FLAGS_use_sdcard && !config_.protected_vm();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (FileExists(instance_.sdcard_path())) {
      return {};
    }
    CF_EXPECT(CreateBlankImage(instance_.sdcard_path(),
                               FLAGS_blank_sdcard_image_mb, "sdcard"),
              "Failed to create \"" << instance_.sdcard_path() << "\"");
    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class InitializeFactoryResetProtected : public SetupFeature {
 public:
  INJECT(InitializeFactoryResetProtected(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeSdCard"; }
  bool Enabled() const override { return !config_.protected_vm(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    auto frp = instance_.factory_reset_protected_path();
    if (FileExists(frp)) {
      return {};
    }
    CF_EXPECT(CreateBlankImage(frp, 1 /* mb */, "none"),
              "Failed to create \"" << frp << "\"");
    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class InitializeInstanceCompositeDisk : public SetupFeature {
 public:
  INJECT(InitializeInstanceCompositeDisk(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance,
      InitializeFactoryResetProtected& frp,
      GeneratePersistentVbmeta& vbmeta))
      : config_(config),
        instance_(instance),
        frp_(frp),
        vbmeta_(vbmeta) {}

  std::string Name() const override {
    return "InitializeInstanceCompositeDisk";
  }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {
        static_cast<SetupFeature*>(&frp_),
        static_cast<SetupFeature*>(&vbmeta_),
    };
  }
  Result<void> ResultSetup() override {
    auto ipath = [this](const std::string& path) -> std::string {
      return instance_.PerInstancePath(path.c_str());
    };
    auto persistent_disk_builder =
        DiskBuilder()
            .Partitions(persistent_composite_disk_config(config_, instance_))
            .VmManager(config_.vm_manager())
            .CrosvmPath(config_.crosvm_binary())
            .ConfigPath(ipath("persistent_composite_disk_config.txt"))
            .HeaderPath(ipath("persistent_composite_gpt_header.img"))
            .FooterPath(ipath("persistent_composite_gpt_footer.img"))
            .CompositeDiskPath(instance_.persistent_composite_disk_path())
            .ResumeIfPossible(FLAGS_resume);

    CF_EXPECT(persistent_disk_builder.BuildCompositeDiskIfNecessary());
    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  InitializeFactoryResetProtected& frp_;
  GeneratePersistentVbmeta& vbmeta_;
};

class VbmetaEnforceMinimumSize : public SetupFeature {
 public:
  INJECT(VbmetaEnforceMinimumSize()) {}

  std::string Name() const override { return "VbmetaEnforceMinimumSize"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    // libavb expects to be able to read the maximum vbmeta size, so we must
    // provide a partition which matches this or the read will fail
    for (const auto& vbmeta_image :
         {FLAGS_vbmeta_image, FLAGS_vbmeta_system_image}) {
      if (FileSize(vbmeta_image) != VBMETA_MAX_SIZE) {
        auto fd = SharedFD::Open(vbmeta_image, O_RDWR);
        CF_EXPECT(fd->IsOpen(), "Could not open \"" << vbmeta_image << "\": "
                                                    << fd->StrError());
        CF_EXPECT(fd->Truncate(VBMETA_MAX_SIZE) == 0,
                  "`truncate --size=" << VBMETA_MAX_SIZE << " " << vbmeta_image
                                      << "` failed: " << fd->StrError());
      }
    }
    return {};
  }
};

class BootloaderPresentCheck : public SetupFeature {
 public:
  INJECT(BootloaderPresentCheck()) {}

  std::string Name() const override { return "BootloaderPresentCheck"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    CF_EXPECT(FileHasContent(FLAGS_bootloader),
              "File not found: " << FLAGS_bootloader);
    return {};
  }
};

static fruit::Component<> DiskChangesComponent(const FetcherConfig* fetcher,
                                               const CuttlefishConfig* config) {
  return fruit::createComponent()
      .bindInstance(*fetcher)
      .bindInstance(*config)
      .addMultibinding<SetupFeature, InitializeMetadataImage>()
      .addMultibinding<SetupFeature, BootImageRepacker>()
      .addMultibinding<SetupFeature, VbmetaEnforceMinimumSize>()
      .addMultibinding<SetupFeature, BootloaderPresentCheck>()
      .addMultibinding<SetupFeature, Gem5ImageUnpacker>()
      .install(FixedMiscImagePathComponent, &FLAGS_misc_image)
      .install(InitializeMiscImageComponent)
      .install(FixedDataImagePathComponent, &FLAGS_data_image)
      .install(InitializeDataImageComponent)
      // Create esp if necessary
      .install(InitializeEspImageComponent, &FLAGS_otheros_esp_image,
               &FLAGS_otheros_kernel_path, &FLAGS_otheros_initramfs_path,
               &FLAGS_otheros_root_image, config)
      .install(SuperImageRebuilderComponent, &FLAGS_super_image);
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
      .addMultibinding<SetupFeature, InitializeFactoryResetProtected>()
      .addMultibinding<SetupFeature, GeneratePersistentBootconfig>()
      .addMultibinding<SetupFeature, GeneratePersistentVbmeta>()
      .addMultibinding<SetupFeature, InitializeInstanceCompositeDisk>()
      .install(InitBootloaderEnvPartitionComponent);
}

Result<void> CreateDynamicDiskFiles(const FetcherConfig& fetcher_config,
                                    const CuttlefishConfig& config) {
  // TODO(schuffelen): Unify this with the other injector created in
  // assemble_cvd.cpp
  fruit::Injector<> injector(DiskChangesComponent, &fetcher_config, &config);

  const auto& features = injector.getMultibindings<SetupFeature>();
  CF_EXPECT(SetupFeature::RunSetup(features));

  for (const auto& instance : config.Instances()) {
    fruit::Injector<> instance_injector(DiskChangesPerInstanceComponent,
                                        &fetcher_config, &config, &instance);
    const auto& instance_features =
        instance_injector.getMultibindings<SetupFeature>();
    CF_EXPECT(SetupFeature::RunSetup(instance_features),
              "instance = \"" << instance.instance_name() << "\"");
  }

  // Check if filling in the sparse image would run out of disk space.
  auto existing_sizes = SparseFileSizes(FLAGS_data_image);
  CF_EXPECT(existing_sizes.sparse_size > 0 || existing_sizes.disk_size > 0,
            "Unable to determine size of \"" << FLAGS_data_image
                                             << "\". Does this file exist?");
  auto available_space = AvailableSpaceAtPath(FLAGS_data_image);
  if (available_space < existing_sizes.sparse_size - existing_sizes.disk_size) {
    // TODO(schuffelen): Duplicate this check in run_cvd when it can run on a
    // separate machine
    return CF_ERR("Not enough space remaining in fs containing \""
                  << FLAGS_data_image << "\", wanted "
                  << (existing_sizes.sparse_size - existing_sizes.disk_size)
                  << ", got " << available_space);
  } else {
    LOG(DEBUG) << "Available space: " << available_space;
    LOG(DEBUG) << "Sparse size of \"" << FLAGS_data_image
               << "\": " << existing_sizes.sparse_size;
    LOG(DEBUG) << "Disk size of \"" << FLAGS_data_image
               << "\": " << existing_sizes.disk_size;
  }

  auto os_disk_builder = OsCompositeDiskBuilder(config);
  auto built_composite =
      CF_EXPECT(os_disk_builder.BuildCompositeDiskIfNecessary());
  if (built_composite) {
    for (auto instance : config.Instances()) {
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
  }

  if (!FLAGS_protected_vm) {
    for (auto instance : config.Instances()) {
      os_disk_builder.OverlayPath(instance.PerInstancePath("overlay.img"));
      CF_EXPECT(os_disk_builder.BuildOverlayIfNecessary());
      if (instance.start_ap()) {
        os_disk_builder.OverlayPath(instance.PerInstancePath("ap_overlay.img"));
        CF_EXPECT(os_disk_builder.BuildOverlayIfNecessary());
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
      RepackGem5BootImage(
          instance.PerInstancePath("initrd.img"),
          instance.persistent_bootconfig_path(),
          config.assembly_dir());
    }
  }

  return {};
}

} // namespace cuttlefish
