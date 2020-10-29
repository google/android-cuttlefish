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

#include <fstream>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/data_image.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/commands/assemble_cvd/assembler_defs.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/boot_image_unpacker.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/commands/assemble_cvd/image_aggregator.h"
#include "host/commands/assemble_cvd/super_image_mixer.h"

// Taken from external/avb/libavb/avb_slot_verify.c; this define is not in the headers
#define VBMETA_MAX_SIZE 65536ul

using cuttlefish::AssemblerExitCodes;
using cuttlefish::CreateBlankImage;
using cuttlefish::DataImageResult;
using cuttlefish::InitializeMiscImage;
using cuttlefish::vm_manager::CrosvmManager;

DEFINE_string(system_image_dir, cuttlefish::DefaultGuestImagePath(""),
              "Location of the system partition images.");

DEFINE_string(boot_image, "",
              "Location of cuttlefish boot image. If empty it is assumed to be "
              "boot.img in the directory specified by -system_image_dir.");
DEFINE_string(cache_image, "", "Location of the cache partition image.");
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

DEFINE_int32(blank_metadata_image_mb, 16,
             "The size of the blank metadata image to generate, MB.");
DEFINE_int32(blank_sdcard_image_mb, 2048,
             "The size of the blank sdcard image to generate, MB.");

DECLARE_string(bootloader);
DECLARE_bool(use_bootloader);
DECLARE_string(initramfs_path);
DECLARE_string(kernel_path);
DECLARE_bool(resume);

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
  std::string default_cache_image = FLAGS_system_image_dir + "/cache.img";
  SetCommandLineOptionWithMode("cache_image", default_cache_image.c_str(),
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

std::unique_ptr<cuttlefish::BootImageUnpacker> CreateBootImageUnpacker() {
  return cuttlefish::BootImageUnpacker::FromImages(
      FLAGS_boot_image, FLAGS_vendor_boot_image);
}

static bool DecompressKernel(const std::string& src, const std::string& dst) {
  cuttlefish::Command decomp_cmd(cuttlefish::DefaultHostArtifactsPath("bin/extract-vmlinux"));
  decomp_cmd.AddParameter(src);
  std::string current_path = getenv("PATH") == nullptr ? "" : getenv("PATH");
  std::string bin_folder = cuttlefish::DefaultHostArtifactsPath("bin");
  decomp_cmd.SetEnvironment({"PATH=" + current_path + ":" + bin_folder});
  auto output_file = cuttlefish::SharedFD::Creat(dst.c_str(), 0666);
  if (!output_file->IsOpen()) {
    LOG(ERROR) << "Unable to create decompressed image file: "
               << output_file->StrError();
    return false;
  }
  decomp_cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdOut, output_file);
  auto decomp_proc = decomp_cmd.Start();
  return decomp_proc.Started() && decomp_proc.Wait() == 0;
}

std::vector<ImagePartition> disk_config(
    const cuttlefish::CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<ImagePartition> partitions;

  // Note that if the positions of env or misc change, the environment for
  // u-boot must be updated as well (see boot_config.cc and
  // configs/cf-x86_defconfig in external/u-boot).
  partitions.push_back(ImagePartition {
    .label = "uboot_env",
    .image_file_path = instance.uboot_env_image_path(),
  });
  partitions.push_back(ImagePartition {
    .label = "misc",
    .image_file_path = FLAGS_misc_image,
  });
  if (FLAGS_use_bootloader) {
    partitions.push_back(ImagePartition {
      .label = "bootloader",
      .image_file_path = FLAGS_bootloader,
    });
  }
  partitions.push_back(ImagePartition {
    .label = "boot_a",
    .image_file_path = FLAGS_boot_image,
  });
  partitions.push_back(ImagePartition {
    .label = "boot_b",
    .image_file_path = FLAGS_boot_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vendor_boot_a",
    .image_file_path = FLAGS_vendor_boot_image,
  });
  partitions.push_back(ImagePartition {
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
    .label = "cache",
    .image_file_path = FLAGS_cache_image,
  });
  partitions.push_back(ImagePartition {
    .label = "metadata",
    .image_file_path = FLAGS_metadata_image,
  });
  return partitions;
}

static std::chrono::system_clock::time_point LastUpdatedInputDisk(
    const cuttlefish::CuttlefishConfig::InstanceSpecific& instance) {
  std::chrono::system_clock::time_point ret;
  for (auto& partition : disk_config(instance)) {
    auto partition_mod_time = cuttlefish::FileModificationTime(partition.image_file_path);
    if (partition_mod_time > ret) {
      ret = partition_mod_time;
    }
  }
  return ret;
}

bool ShouldCreateAllCompositeDisks(const cuttlefish::CuttlefishConfig& config) {
  std::chrono::system_clock::time_point youngest_disk_img;
  for (auto& partition : disk_config(config.ForDefaultInstance())) {
    if (partition.label == "uboot_env") {
      continue;
    }

    auto partition_mod_time = cuttlefish::FileModificationTime(partition.image_file_path);
    if (partition_mod_time > youngest_disk_img) {
      youngest_disk_img = partition_mod_time;
    }
  }

  // If the youngest partition img is younger than any composite disk, this fact implies that
  // the composite disks are all out of date and need to be reinitialized.
  for (auto& instance : config.Instances()) {
    if (!cuttlefish::FileExists(instance.composite_disk_path())) {
      continue;
    }
    if (youngest_disk_img > cuttlefish::FileModificationTime(instance.composite_disk_path())) {
      return true;
    }
  }

  return false;
}

bool DoesCompositeMatchCurrentDiskConfig(
    const cuttlefish::CuttlefishConfig::InstanceSpecific& instance) {
  std::string prior_disk_config_path = instance.PerInstancePath("disk_config.txt");
  std::string current_disk_config_path = instance.PerInstancePath("disk_config.txt.tmp");
  std::ostringstream disk_conf;
  for (auto& partition : disk_config(instance)) {
    disk_conf << partition.image_file_path << "\n";
  }

  {
    // This file acts as a descriptor of the cuttlefish disk contents in a VMM agnostic way (VMMs
    // used are QEMU and CrosVM at the time of writing). This file is used to determine if the
    // disk config for the pending boot matches the disk from the past boot.
    std::ofstream file_out(current_disk_config_path.c_str(), std::ios::binary);
    file_out << disk_conf.str();
    if (!file_out.good()) {
      exit(cuttlefish::kDiskConfigVerificationError);
    }
  }

  if (!cuttlefish::FileExists(prior_disk_config_path) ||
      cuttlefish::ReadFile(prior_disk_config_path) != cuttlefish::ReadFile(current_disk_config_path)) {
    if (!cuttlefish::RenameFile(current_disk_config_path, prior_disk_config_path)) {
      LOG(ERROR) << "Unable to delete the old disk config descriptor";
      exit(cuttlefish::kDiskConfigVerificationError);
    }
    LOG(DEBUG) << "Disk Config has changed since last boot. Regenerating composite disk.";
    return false;
  } else {
    cuttlefish::RemoveFile(current_disk_config_path);
    return true;
  }
}

bool ShouldCreateCompositeDisk(const cuttlefish::CuttlefishConfig::InstanceSpecific& instance) {
  if (!cuttlefish::FileExists(instance.composite_disk_path())) {
    return true;
  }

  auto composite_age = cuttlefish::FileModificationTime(instance.composite_disk_path());
  return composite_age < LastUpdatedInputDisk(instance);
}

static bool ConcatRamdisks(
    const std::string& new_ramdisk_path,
    const std::string& ramdisk_a_path,
    const std::string& ramdisk_b_path) {
  // clear out file of any pre-existing content
  std::ofstream new_ramdisk(new_ramdisk_path, std::ios_base::binary | std::ios_base::trunc);
  std::ifstream ramdisk_a(ramdisk_a_path, std::ios_base::binary);
  std::ifstream ramdisk_b(ramdisk_b_path, std::ios_base::binary);

  if (!new_ramdisk.is_open() || !ramdisk_a.is_open() || !ramdisk_b.is_open()) {
    return false;
  }

  new_ramdisk << ramdisk_a.rdbuf() << ramdisk_b.rdbuf();
  return true;
}

static off_t AvailableSpaceAtPath(const std::string& path) {
  struct statvfs vfs;
  if (statvfs(path.c_str(), &vfs) != 0) {
    int error_num = errno;
    LOG(ERROR) << "Could not find space available at " << path << ", error was "
               << strerror(error_num);
    return 0;
  }
  return vfs.f_bsize * vfs.f_bavail; // block size * free blocks for unprivileged users
}

bool CreateCompositeDisk(const cuttlefish::CuttlefishConfig& config,
                         const cuttlefish::CuttlefishConfig::InstanceSpecific& instance) {
  if (!cuttlefish::SharedFD::Open(instance.composite_disk_path().c_str(),
                                  O_WRONLY | O_CREAT, 0644)->IsOpen()) {
    LOG(ERROR) << "Could not ensure " << instance.composite_disk_path() << " exists";
    return false;
  }
  if (config.vm_manager() == CrosvmManager::name()) {
    // Check if filling in the sparse image would run out of disk space.
    auto existing_sizes = cuttlefish::SparseFileSizes(FLAGS_data_image);
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
    std::string header_path = instance.PerInstancePath("gpt_header.img");
    std::string footer_path = instance.PerInstancePath("gpt_footer.img");
    CreateCompositeDisk(disk_config(instance), header_path, footer_path,
                        instance.composite_disk_path());
  } else {
    // If this doesn't fit into the disk, it will fail while aggregating. The
    // aggregator doesn't maintain any sparse attributes.
    AggregateImage(disk_config(instance), instance.composite_disk_path());
  }
  return true;
}

const std::string kKernelDefaultPath = "kernel";
const std::string kInitramfsImg = "initramfs.img";
const std::string kRamdiskConcatExt = ".concat";

void CreateDynamicDiskFiles(const cuttlefish::FetcherConfig& fetcher_config,
                            const cuttlefish::CuttlefishConfig* config,
                            cuttlefish::BootImageUnpacker* boot_img_unpacker) {

  if (!cuttlefish::FileHasContent(FLAGS_boot_image)) {
    LOG(ERROR) << "File not found: " << FLAGS_boot_image;
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  if (!cuttlefish::FileHasContent(FLAGS_vendor_boot_image)) {
    LOG(ERROR) << "File not found: " << FLAGS_vendor_boot_image;
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  if (!FLAGS_use_bootloader &&
      !boot_img_unpacker->Unpack(config->ramdisk_image_path(),
                                 config->vendor_ramdisk_image_path(),
                                 config->use_unpacked_kernel()
                                     ? config->kernel_image_path()
                                     : "")) {
    LOG(ERROR) << "Failed to unpack boot image";
    exit(AssemblerExitCodes::kBootImageUnpackError);
  }

  std::string discovered_kernel = fetcher_config.FindCvdFileWithSuffix(kKernelDefaultPath);
  std::string foreign_kernel = FLAGS_kernel_path.size() ? FLAGS_kernel_path : discovered_kernel;
  std::string discovered_ramdisk = fetcher_config.FindCvdFileWithSuffix(kInitramfsImg);
  std::string foreign_ramdisk = FLAGS_initramfs_path.size () ? FLAGS_initramfs_path : discovered_ramdisk;
  std::string new_boot_image_path = config->AssemblyPath("boot_repacked.img");
  std::string new_vendor_boot_image_path = config->AssemblyPath("vendor_boot_repacked.img");
  if (FLAGS_use_bootloader && (foreign_kernel.size() || foreign_ramdisk.size())) {
    // Repack the boot images if kernels and/or ramdisks are passed in.
    if (foreign_kernel.size()) {
      if (cuttlefish::RepackBootImage(foreign_kernel, FLAGS_boot_image, new_boot_image_path,
          config->assembly_dir())) {
        SetCommandLineOptionWithMode("boot_image", new_boot_image_path.c_str(),
                                     google::FlagSettingMode::SET_FLAGS_DEFAULT);
      } else {
        LOG(ERROR) << "Failed to regenerate the boot image with the new kernel";
        exit(AssemblerExitCodes::kBootImgRepackError);
      }
    }
    if (foreign_ramdisk.size()) {
      if (cuttlefish::RepackVendorBootImage(foreign_ramdisk, FLAGS_vendor_boot_image,
                            new_vendor_boot_image_path, config->assembly_dir())) {
        SetCommandLineOptionWithMode("vendor_boot_image", new_vendor_boot_image_path.c_str(),
                                     google::FlagSettingMode::SET_FLAGS_DEFAULT);
      } else {
        LOG(ERROR) << "Failed to regenerate the vendor boot image with the new ramdisk";
        exit(AssemblerExitCodes::kBootImgRepackError);
      }
    }
    if (foreign_kernel.size() && !foreign_ramdisk.size()) {
      if (cuttlefish::RepackVendorBootImageWithEmptyRamdisk(FLAGS_vendor_boot_image,
          new_vendor_boot_image_path, config->assembly_dir())) {
        SetCommandLineOptionWithMode("vendor_boot_image", new_vendor_boot_image_path.c_str(),
                                     google::FlagSettingMode::SET_FLAGS_DEFAULT);
      } else {
        LOG(ERROR) << "Failed to regenerate the vendor boot image without a ramdisk";
        exit(AssemblerExitCodes::kBootImgRepackError);
      }
    }
  } else if (!FLAGS_use_bootloader && (!foreign_kernel.size() || foreign_ramdisk.size())) {
    // This code path is taken when the virtual device kernel is launched
    // directly by the hypervisor instead of the bootloader.
    // This code path takes care of all the ramdisk processing that the
    // bootloader normally does.
    const std::string& vendor_ramdisk_path =
      config->initramfs_path().size() ? config->initramfs_path()
                                      : config->vendor_ramdisk_image_path();
    if (!ConcatRamdisks(config->final_ramdisk_path(),
                       config->ramdisk_image_path(), vendor_ramdisk_path)) {
      LOG(ERROR) << "Failed to concatenate ramdisk and vendor ramdisk";
      exit(AssemblerExitCodes::kInitRamFsConcatError);
    }
  }

  if (config->decompress_kernel()) {
    if (!DecompressKernel(config->kernel_image_path(),
        config->decompressed_kernel_image_path())) {
      LOG(ERROR) << "Failed to decompress kernel";
      exit(AssemblerExitCodes::kKernelDecompressError);
    }
  }

  // Create misc if necessary
  if (!InitializeMiscImage(FLAGS_misc_image)) {
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  // Create data if necessary
  DataImageResult dataImageResult = ApplyDataImagePolicy(*config, FLAGS_data_image);
  if (dataImageResult == DataImageResult::Error) {
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  // Create boot_config if necessary
  for (auto instance : config->Instances()) {
    if (!InitBootloaderEnvPartition(*config, instance)) {
      exit(cuttlefish::kCuttlefishConfigurationInitError);
    }
  }

  if (!cuttlefish::FileExists(FLAGS_metadata_image)) {
    CreateBlankImage(FLAGS_metadata_image, FLAGS_blank_metadata_image_mb, "none");
  }

  for (const auto& instance : config->Instances()) {
    if (!cuttlefish::FileExists(instance.access_kregistry_path())) {
      CreateBlankImage(instance.access_kregistry_path(), 2 /* mb */, "none");
    }

    if (!cuttlefish::FileExists(instance.pstore_path())) {
      CreateBlankImage(instance.pstore_path(), 2 /* mb */, "none");
    }

    if (!cuttlefish::FileExists(instance.sdcard_path())) {
      CreateBlankImage(instance.sdcard_path(),
                       FLAGS_blank_sdcard_image_mb, "sdcard");
    }
  }

  // libavb expects to be able to read the maximum vbmeta size, so we must
  // provide a partition which matches this or the read will fail
  for (const auto& vbmeta_image : { FLAGS_vbmeta_image, FLAGS_vbmeta_system_image }) {
    if (cuttlefish::FileSize(vbmeta_image) != VBMETA_MAX_SIZE) {
      auto fd = cuttlefish::SharedFD::Open(vbmeta_image, O_RDWR);
      if (fd->Truncate(VBMETA_MAX_SIZE) != 0) {
        LOG(ERROR) << "`truncate --size=" << VBMETA_MAX_SIZE << " "
                   << vbmeta_image << "` failed: " << fd->StrError();
        exit(cuttlefish::kCuttlefishConfigurationInitError);
      }
    }
  }

  if (FLAGS_use_bootloader && !cuttlefish::FileHasContent(FLAGS_bootloader)) {
    LOG(ERROR) << "File not found: " << FLAGS_bootloader;
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  if (SuperImageNeedsRebuilding(fetcher_config, *config)) {
    if (!RebuildSuperImage(fetcher_config, *config, FLAGS_super_image)) {
      LOG(ERROR) << "Super image rebuilding requested but could not be completed.";
      exit(cuttlefish::kCuttlefishConfigurationInitError);
    }
  }

  bool newDataImage = dataImageResult == DataImageResult::FileUpdated;

  for (auto instance : config->Instances()) {
    bool compositeMatchesDiskConfig = DoesCompositeMatchCurrentDiskConfig(instance);
    bool oldCompositeDisk = ShouldCreateCompositeDisk(instance);
    auto overlay_path = instance.PerInstancePath("overlay.img");
    bool missingOverlay = !cuttlefish::FileExists(overlay_path);
    bool newOverlay = cuttlefish::FileModificationTime(overlay_path)
        < cuttlefish::FileModificationTime(instance.composite_disk_path());
    if (!compositeMatchesDiskConfig || missingOverlay || oldCompositeDisk || !FLAGS_resume ||
        newDataImage || newOverlay) {
      if (FLAGS_resume) {
        LOG(INFO) << "Requested to continue an existing session, (the default) "
                  << "but the disk files have become out of date. Wiping the "
                  << "old session files and starting a new session for device "
                  << instance.serial_number();
      }
      if (!CreateCompositeDisk(*config, instance)) {
        exit(cuttlefish::kDiskSpaceError);
      }
      CreateQcowOverlay(config->crosvm_binary(), instance.composite_disk_path(), overlay_path);
      CreateBlankImage(instance.access_kregistry_path(), 2 /* mb */, "none");
      CreateBlankImage(instance.pstore_path(), 2 /* mb */, "none");
    }
  }

  for (auto instance : config->Instances()) {
    // Check that the files exist
    for (const auto& file : instance.virtual_disk_paths()) {
      if (!file.empty() && !cuttlefish::FileHasContent(file.c_str())) {
        LOG(ERROR) << "File not found: " << file;
        exit(cuttlefish::kCuttlefishConfigurationInitError);
      }
    }
  }
}
