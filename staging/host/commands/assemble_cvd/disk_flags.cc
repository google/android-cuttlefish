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
#include "host/commands/assemble_cvd/super_image_mixer.h"
#include "host/libs/config/bootconfig_args.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/data_image.h"
#include "host/libs/image_aggregator/image_aggregator.h"
#include "host/libs/vm_manager/crosvm_manager.h"

// Taken from external/avb/libavb/avb_slot_verify.c; this define is not in the headers
#define VBMETA_MAX_SIZE 65536ul

DECLARE_string(system_image_dir);

DEFINE_string(boot_image, "",
              "Location of cuttlefish boot image. If empty it is assumed to be "
              "boot.img in the directory specified by -system_image_dir.");
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

DECLARE_string(bootloader);
DECLARE_bool(use_sdcard);
DECLARE_string(initramfs_path);
DECLARE_string(kernel_path);
DECLARE_bool(resume);
DECLARE_bool(protected_vm);

namespace cuttlefish {

using vm_manager::CrosvmManager;

bool ResolveInstanceFiles() {
  if (FLAGS_system_image_dir.empty()) {
    LOG(ERROR) << "--system_image_dir must be specified.";
    return false;
  }

  // If user did not specify location of either of these files, expect them to
  // be placed in --system_image_dir location.
  std::string default_boot_image = FLAGS_system_image_dir + "/boot.img";
  SetCommandLineOptionWithMode("boot_image", default_boot_image.c_str(),
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

  return true;
}

std::vector<ImagePartition> os_composite_disk_config() {
  std::vector<ImagePartition> partitions;
  partitions.push_back(ImagePartition {
    .label = "misc",
    .image_file_path = FLAGS_misc_image,
  });
  partitions.push_back(ImagePartition {
    .label = "boot_a",
    .image_file_path = FLAGS_boot_image,
  });
  partitions.push_back(ImagePartition {
    .label = "boot_b",
    .image_file_path = FLAGS_boot_image,
  });
  partitions.push_back(ImagePartition{
      .label = "vendor_boot_a",
      .image_file_path = FLAGS_vendor_boot_image,
  });
  partitions.push_back(ImagePartition{
      .label = "vendor_boot_b",
      .image_file_path = FLAGS_vendor_boot_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vbmeta_a",
    .image_file_path = FLAGS_vbmeta_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vbmeta_b",
    .image_file_path = FLAGS_vbmeta_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vbmeta_system_a",
    .image_file_path = FLAGS_vbmeta_system_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vbmeta_system_b",
    .image_file_path = FLAGS_vbmeta_system_image,
  });
  partitions.push_back(ImagePartition {
    .label = "super",
    .image_file_path = FLAGS_super_image,
  });
  partitions.push_back(ImagePartition {
    .label = "userdata",
    .image_file_path = FLAGS_data_image,
  });
  partitions.push_back(ImagePartition {
    .label = "metadata",
    .image_file_path = FLAGS_metadata_image,
  });
  if (!FLAGS_otheros_root_image.empty()) {
    partitions.push_back(ImagePartition{
        .label = "otheros_esp",
        .image_file_path = FLAGS_otheros_esp_image,
        .type = kEfiSystemPartition,
    });
    partitions.push_back(ImagePartition{
        .label = "otheros_root",
        .image_file_path = FLAGS_otheros_root_image,
    });
  }
  return partitions;
}

std::vector<ImagePartition> persistent_composite_disk_config(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<ImagePartition> partitions;

  // Note that if the position of uboot_env changes, the environment for
  // u-boot must be updated as well (see boot_config.cc and
  // cuttlefish.fragment in external/u-boot).
  partitions.push_back(ImagePartition{
      .label = "uboot_env",
      .image_file_path = instance.uboot_env_image_path(),
  });
  if (!FLAGS_protected_vm) {
    partitions.push_back(ImagePartition{
        .label = "frp",
        .image_file_path = instance.factory_reset_protected_path(),
    });
  }
  partitions.push_back(ImagePartition{
      .label = "bootconfig",
      .image_file_path = instance.persistent_bootconfig_path(),
  });
  return partitions;
}

static std::chrono::system_clock::time_point LastUpdatedInputDisk(
    const std::vector<ImagePartition>& partitions) {
  std::chrono::system_clock::time_point ret;
  for (auto& partition : partitions) {
    if (partition.label == "frp") {
      continue;
    }

    auto partition_mod_time = FileModificationTime(partition.image_file_path);
    if (partition_mod_time > ret) {
      ret = partition_mod_time;
    }
  }
  return ret;
}

bool DoesCompositeMatchCurrentDiskConfig(
    const std::string& prior_disk_config_path,
    const std::vector<ImagePartition>& partitions) {
  std::string current_disk_config_path = prior_disk_config_path + ".tmp";
  std::ostringstream disk_conf;
  for (auto& partition : partitions) {
    disk_conf << partition.image_file_path << "\n";
  }

  {
    // This file acts as a descriptor of the cuttlefish disk contents in a VMM agnostic way (VMMs
    // used are QEMU and CrosVM at the time of writing). This file is used to determine if the
    // disk config for the pending boot matches the disk from the past boot.
    std::ofstream file_out(current_disk_config_path.c_str(), std::ios::binary);
    file_out << disk_conf.str();
    CHECK(file_out.good()) << "Disk config verification failed.";
  }

  if (!FileExists(prior_disk_config_path) ||
      ReadFile(prior_disk_config_path) != ReadFile(current_disk_config_path)) {
    CHECK(cuttlefish::RenameFile(current_disk_config_path, prior_disk_config_path))
        << "Unable to delete the old disk config descriptor";
    LOG(DEBUG) << "Disk Config has changed since last boot. Regenerating composite disk.";
    return false;
  } else {
    RemoveFile(current_disk_config_path);
    return true;
  }
}

bool ShouldCreateCompositeDisk(const std::string& composite_disk_path,
                               const std::vector<ImagePartition>& partitions) {
  if (!FileExists(composite_disk_path)) {
    return true;
  }

  auto composite_age = FileModificationTime(composite_disk_path);
  return composite_age < LastUpdatedInputDisk(partitions);
}

bool ShouldCreateOsCompositeDisk(const CuttlefishConfig& config) {
  return ShouldCreateCompositeDisk(config.os_composite_disk_path(),
                                   os_composite_disk_config());
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

bool CreateOsCompositeDisk(const CuttlefishConfig& config) {
  if (!SharedFD::Open(config.os_composite_disk_path().c_str(),
                      O_WRONLY | O_CREAT, 0644)
           ->IsOpen()) {
    LOG(ERROR) << "Could not ensure " << config.os_composite_disk_path()
               << " exists";
    return false;
  }
  if (config.vm_manager() == CrosvmManager::name()) {
    // Check if filling in the sparse image would run out of disk space.
    auto existing_sizes = SparseFileSizes(FLAGS_data_image);
    if (existing_sizes.sparse_size == 0 && existing_sizes.disk_size == 0) {
      LOG(ERROR) << "Unable to determine size of \"" << FLAGS_data_image
                 << "\". Does this file exist?";
    }
    auto available_space = AvailableSpaceAtPath(FLAGS_data_image);
    if (available_space < existing_sizes.sparse_size - existing_sizes.disk_size) {
      // TODO(schuffelen): Duplicate this check in run_cvd when it can run on a separate machine
      LOG(ERROR) << "Not enough space remaining in fs containing " << FLAGS_data_image;
      LOG(ERROR) << "Wanted " << (existing_sizes.sparse_size - existing_sizes.disk_size);
      LOG(ERROR) << "Got " << available_space;
      return false;
    } else {
      LOG(DEBUG) << "Available space: " << available_space;
      LOG(DEBUG) << "Sparse size of \"" << FLAGS_data_image << "\": "
                 << existing_sizes.sparse_size;
      LOG(DEBUG) << "Disk size of \"" << FLAGS_data_image << "\": "
                 << existing_sizes.disk_size;
    }
    std::string header_path =
        config.AssemblyPath("os_composite_gpt_header.img");
    std::string footer_path =
        config.AssemblyPath("os_composite_gpt_footer.img");
    CreateCompositeDisk(os_composite_disk_config(), header_path, footer_path,
                        config.os_composite_disk_path());
  } else {
    // If this doesn't fit into the disk, it will fail while aggregating. The
    // aggregator doesn't maintain any sparse attributes.
    AggregateImage(os_composite_disk_config(), config.os_composite_disk_path());
  }
  return true;
}

bool CreatePersistentCompositeDisk(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (!SharedFD::Open(instance.persistent_composite_disk_path().c_str(),
                      O_WRONLY | O_CREAT, 0644)
           ->IsOpen()) {
    LOG(ERROR) << "Could not ensure "
               << instance.persistent_composite_disk_path() << " exists";
    return false;
  }
  if (config.vm_manager() == CrosvmManager::name()) {
    std::string header_path =
        instance.PerInstancePath("persistent_composite_gpt_header.img");
    std::string footer_path =
        instance.PerInstancePath("persistent_composite_gpt_footer.img");
    CreateCompositeDisk(persistent_composite_disk_config(instance), header_path,
                        footer_path, instance.persistent_composite_disk_path());
  } else {
    AggregateImage(persistent_composite_disk_config(instance),
                   instance.persistent_composite_disk_path());
  }
  return true;
}

static void RepackAllBootImages(const CuttlefishConfig& config) {
  CHECK(FileHasContent(FLAGS_boot_image))
      << "File not found: " << FLAGS_boot_image;

  CHECK(FileHasContent(FLAGS_vendor_boot_image))
      << "File not found: " << FLAGS_vendor_boot_image;

  if (FLAGS_kernel_path.size()) {
    const std::string new_boot_image_path =
        config.AssemblyPath("boot_repacked.img");
    bool success = RepackBootImage(FLAGS_kernel_path, FLAGS_boot_image,
                                   new_boot_image_path, config.assembly_dir());
    CHECK(success) << "Failed to regenerate the boot image with the new kernel";
    SetCommandLineOptionWithMode("boot_image", new_boot_image_path.c_str(),
                                 google::FlagSettingMode::SET_FLAGS_DEFAULT);
  }

  if (FLAGS_kernel_path.size() || FLAGS_initramfs_path.size()) {
    const std::string new_vendor_boot_image_path =
        config.AssemblyPath("vendor_boot_repacked.img");
    // Repack the vendor boot images if kernels and/or ramdisks are passed in.
    if (FLAGS_initramfs_path.size()) {
      bool success = RepackVendorBootImage(
          FLAGS_initramfs_path, FLAGS_vendor_boot_image,
          new_vendor_boot_image_path, config.assembly_dir(),
          config.bootconfig_supported());
      CHECK(success) << "Failed to regenerate the vendor boot image with the "
                        "new ramdisk";
    } else {
      // This control flow implies a kernel with all configs built in.
      // If it's just the kernel, repack the vendor boot image without a
      // ramdisk.
      bool success = RepackVendorBootImageWithEmptyRamdisk(
          FLAGS_vendor_boot_image, new_vendor_boot_image_path,
          config.assembly_dir(), config.bootconfig_supported());
      CHECK(success)
          << "Failed to regenerate the vendor boot image without a ramdisk";
    }
    SetCommandLineOptionWithMode("vendor_boot_image",
                                 new_vendor_boot_image_path.c_str(),
                                 google::FlagSettingMode::SET_FLAGS_DEFAULT);
  }
}

static void GeneratePersistentBootconfig(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  const auto bootconfig_path = instance.persistent_bootconfig_path();
  if (!FileExists(bootconfig_path)) {
    CreateBlankImage(bootconfig_path, 1 /* mb */, "none");
  }

  auto bootconfig_fd = SharedFD::Open(bootconfig_path, O_RDWR);
  CHECK(bootconfig_fd->IsOpen())
      << "Unable to open bootconfig file: " << bootconfig_fd->StrError();

  //  Cuttlefish for the time being won't be able to support OTA from a
  //  non-bootconfig kernel to a bootconfig-kernel (or vice versa) IF the device
  //  is stopped (via stop_cvd). This is rarely an issue since OTA testing run
  //  on cuttlefish is done within one launch cycle of the device. If this ever
  //  becomes an issue, this code will have to be rewritten.
  if (!config.bootconfig_supported()) {
    return;
  }

  const std::string bootconfig =
      android::base::Join(BootconfigArgsFromConfig(config, instance), "\n") +
      "\n";
  ssize_t bytesWritten = WriteAll(bootconfig_fd, bootconfig);
  CHECK(bytesWritten == bootconfig.size());
  LOG(DEBUG) << "Bootconfig parameters from vendor boot image and config are "
             << ReadFile(bootconfig_path);

  const off_t bootconfig_size_bytes =
      AlignToPowerOf2(bootconfig.size(), PARTITION_SIZE_SHIFT);
  CHECK(bootconfig_fd->Truncate(bootconfig_size_bytes) == 0)
      << "`truncate --size=" << bootconfig_size_bytes << " bytes "
      << bootconfig_path << "` failed:" << bootconfig_fd->StrError();
}

class InitializeMetadataImage : public Feature {
 public:
  INJECT(InitializeMetadataImage()) {}

  // Feature
  std::string Name() const override { return "InitializeMetadataImage"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }
  bool Enabled() const override { return true; }

 private:
  bool Setup() {
    if (!FileExists(FLAGS_metadata_image)) {
      bool success = CreateBlankImage(FLAGS_metadata_image,
                                      FLAGS_blank_metadata_image_mb, "none");
      if (!success) {
        LOG(ERROR) << "Failed to create \"" << FLAGS_metadata_image
                   << "\" with size " << FLAGS_blank_metadata_image_mb;
      }
      return success;
    }
    return true;
  }
};

static fruit::Component<> DiskChangesComponent(const FetcherConfig* fetcher,
                                               const CuttlefishConfig* config) {
  return fruit::createComponent()
      .bindInstance(*fetcher)
      .bindInstance(*config)
      .addMultibinding<Feature, InitializeMetadataImage>()
      .install(FixedMiscImagePathComponent, &FLAGS_misc_image)
      .install(InitializeMiscImageComponent)
      .install(FixedDataImagePathComponent, &FLAGS_data_image)
      .install(InitializeDataImageComponent);
}

void CreateDynamicDiskFiles(const FetcherConfig& fetcher_config,
                            const CuttlefishConfig& config) {
  // TODO(schuffelen): Unify this with the other injector created in
  // assemble_cvd.cpp
  fruit::Injector<> injector(DiskChangesComponent, &fetcher_config, &config);

  const auto& features = injector.getMultibindings<Feature>();
  CHECK(Feature::RunSetup(features)) << "Failed to run feature setup.";

  // Create esp if necessary
  if (!FLAGS_otheros_root_image.empty()) {
    CHECK(InitializeEspImage(FLAGS_otheros_esp_image, FLAGS_otheros_kernel_path,
                             FLAGS_otheros_initramfs_path))
        << "Failed to create esp image";
  }

  // If we are booting a protected VM, for now, assume we want a super minimal
  // environment with no userdata encryption, limited debug, no FRP emulation, a
  // static env for the bootloader, no SD-Card and no resume-on-reboot HAL
  // support. We can also assume that image repacking isn't trusted. Repacking
  // requires resigning the image and keys from an android host aren't trusted.
  if (!FLAGS_protected_vm) {
    RepackAllBootImages(config);

    for (const auto& instance : config.Instances()) {
      if (!FileExists(instance.access_kregistry_path())) {
        CreateBlankImage(instance.access_kregistry_path(), 2 /* mb */, "none");
      }

      if (!FileExists(instance.pstore_path())) {
        CreateBlankImage(instance.pstore_path(), 2 /* mb */, "none");
      }

      if (FLAGS_use_sdcard && !FileExists(instance.sdcard_path())) {
        CreateBlankImage(instance.sdcard_path(),
                         FLAGS_blank_sdcard_image_mb, "sdcard");
      }

      CHECK(InitBootloaderEnvPartition(config, instance))
          << "Failed to create bootloader environment partition";

      const auto frp = instance.factory_reset_protected_path();
      if (!FileExists(frp)) {
        CreateBlankImage(frp, 1 /* mb */, "none");
      }

      GeneratePersistentBootconfig(config, instance);
    }
  }

  for (const auto& instance : config.Instances()) {
    bool compositeMatchesDiskConfig = DoesCompositeMatchCurrentDiskConfig(
        instance.PerInstancePath("persistent_composite_disk_config.txt"),
        persistent_composite_disk_config(instance));
    bool oldCompositeDisk =
        ShouldCreateCompositeDisk(instance.persistent_composite_disk_path(),
                                  persistent_composite_disk_config(instance));

    if (!compositeMatchesDiskConfig || oldCompositeDisk) {
      CHECK(CreatePersistentCompositeDisk(config, instance))
          << "Failed to create persistent composite disk";
    }
  }

  // libavb expects to be able to read the maximum vbmeta size, so we must
  // provide a partition which matches this or the read will fail
  for (const auto& vbmeta_image : { FLAGS_vbmeta_image, FLAGS_vbmeta_system_image }) {
    if (FileSize(vbmeta_image) != VBMETA_MAX_SIZE) {
      auto fd = SharedFD::Open(vbmeta_image, O_RDWR);
      CHECK(fd->Truncate(VBMETA_MAX_SIZE) == 0)
        << "`truncate --size=" << VBMETA_MAX_SIZE << " " << vbmeta_image << "` "
        << "failed: " << fd->StrError();
    }
  }

  CHECK(FileHasContent(FLAGS_bootloader))
      << "File not found: " << FLAGS_bootloader;

  if (SuperImageNeedsRebuilding(fetcher_config, config)) {
    bool success = RebuildSuperImage(fetcher_config, config, FLAGS_super_image);
    CHECK(success) << "Super image rebuilding requested but could not be completed.";
  }

  bool oldOsCompositeDisk = ShouldCreateOsCompositeDisk(config);
  bool osCompositeMatchesDiskConfig = DoesCompositeMatchCurrentDiskConfig(
      config.AssemblyPath("os_composite_disk_config.txt"),
      os_composite_disk_config());
  if (!osCompositeMatchesDiskConfig || oldOsCompositeDisk || !FLAGS_resume) {
    CHECK(CreateOsCompositeDisk(config))
        << "Failed to create OS composite disk";

    for (auto instance : config.Instances()) {
      if (FLAGS_resume) {
        LOG(INFO) << "Requested to continue an existing session, (the default) "
                  << "but the disk files have become out of date. Wiping the "
                  << "old session files and starting a new session for device "
                  << instance.serial_number();
      }
      if (FileExists(instance.access_kregistry_path())) {
        CreateBlankImage(instance.access_kregistry_path(), 2 /* mb */, "none");
      }
      if (FileExists(instance.pstore_path())) {
        CreateBlankImage(instance.pstore_path(), 2 /* mb */, "none");
      }
    }
  }

  if (!FLAGS_protected_vm) {
    for (auto instance : config.Instances()) {
      auto overlay_path = instance.PerInstancePath("overlay.img");
      bool missingOverlay = !FileExists(overlay_path);
      bool newOverlay = FileModificationTime(overlay_path) <
                        FileModificationTime(config.os_composite_disk_path());
      if (missingOverlay || !FLAGS_resume || newOverlay) {
        CreateQcowOverlay(config.crosvm_binary(),
                          config.os_composite_disk_path(), overlay_path);
      }
    }
  }

  for (auto instance : config.Instances()) {
    // Check that the files exist
    for (const auto& file : instance.virtual_disk_paths()) {
      if (!file.empty()) {
        CHECK(FileHasContent(file)) << "File not found: " << file;
      }
    }
  }
}

} // namespace cuttlefish
