#include "host/libs/config/data_image.h"

#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"

#include "host/libs/config/mbr.h"

namespace cuttlefish {

namespace {
const std::string kDataPolicyUseExisting = "use_existing";
const std::string kDataPolicyCreateIfMissing = "create_if_missing";
const std::string kDataPolicyAlwaysCreate = "always_create";
const std::string kDataPolicyResizeUpTo= "resize_up_to";

const int FSCK_ERROR_CORRECTED = 1;
const int FSCK_ERROR_CORRECTED_REQUIRES_REBOOT = 2;

// Currently the Cuttlefish bootloaders are built only for x86 (32-bit),
// ARM (QEMU only, 32-bit) and AArch64 (64-bit), and U-Boot will hard-code
// these search paths. Install all bootloaders to one of these paths.
// NOTE: For now, just ignore the 32-bit ARM version, as Debian doesn't
//       build an EFI monolith for this architecture.
const std::string kBootPathIA32 = "EFI/BOOT/BOOTIA32.EFI";
const std::string kBootPathAA64 = "EFI/BOOT/BOOTAA64.EFI";

// These are the paths Debian installs the monoliths to. If another distro
// uses an alternative monolith path, add it to this table
const std::pair<std::string, std::string> kGrubBlobTable[] = {
    {"/usr/lib/grub/i386-efi/monolithic/grubia32.efi", kBootPathIA32},
    {"/usr/lib/grub/arm64-efi/monolithic/grubaa64.efi", kBootPathAA64},
};

bool ForceFsckImage(const char* data_image) {
  auto fsck_path = HostBinaryPath("fsck.f2fs");
  int fsck_status = execute({fsck_path, "-y", "-f", data_image});
  if (fsck_status & ~(FSCK_ERROR_CORRECTED|FSCK_ERROR_CORRECTED_REQUIRES_REBOOT)) {
    LOG(ERROR) << "`fsck.f2fs -y -f " << data_image << "` failed with code "
               << fsck_status;
    return false;
  }
  return true;
}

bool NewfsMsdos(const std::string& data_image, int data_image_mb,
                int offset_num_mb) {
  off_t image_size_bytes = static_cast<off_t>(data_image_mb) << 20;
  off_t offset_size_bytes = static_cast<off_t>(offset_num_mb) << 20;
  image_size_bytes -= offset_size_bytes;
  off_t image_size_sectors = image_size_bytes / 512;
  auto newfs_msdos_path = HostBinaryPath("newfs_msdos");
  return execute({newfs_msdos_path,
                         "-F",
                         "32",
                         "-m",
                         "0xf8",
                         "-o",
                         "0",
                         "-c",
                         "8",
                         "-h",
                         "255",
                         "-u",
                         "63",
                         "-S",
                         "512",
                         "-s",
                         std::to_string(image_size_sectors),
                         "-C",
                         std::to_string(data_image_mb) + "M",
                         "-@",
                         std::to_string(offset_size_bytes),
                         data_image}) == 0;
}

bool ResizeImage(const char* data_image, int data_image_mb) {
  auto file_mb = FileSize(data_image) >> 20;
  if (file_mb > data_image_mb) {
    LOG(ERROR) << data_image << " is already " << file_mb << " MB, will not "
               << "resize down.";
    return false;
  } else if (file_mb == data_image_mb) {
    LOG(INFO) << data_image << " is already the right size";
    return true;
  } else {
    off_t raw_target = static_cast<off_t>(data_image_mb) << 20;
    auto fd = SharedFD::Open(data_image, O_RDWR);
    if (fd->Truncate(raw_target) != 0) {
      LOG(ERROR) << "`truncate --size=" << data_image_mb << "M "
                  << data_image << "` failed:" << fd->StrError();
      return false;
    }
    bool fsck_success = ForceFsckImage(data_image);
    if (!fsck_success) {
      return false;
    }
    auto resize_path = HostBinaryPath("resize.f2fs");
    int resize_status = execute({resize_path, data_image});
    if (resize_status != 0) {
      LOG(ERROR) << "`resize.f2fs " << data_image << "` failed with code "
                 << resize_status;
      return false;
    }
    fsck_success = ForceFsckImage(data_image);
    if (!fsck_success) {
      return false;
    }
  }
  return true;
}
} // namespace

void CreateBlankImage(
    const std::string& image, int num_mb, const std::string& image_fmt) {
  LOG(DEBUG) << "Creating " << image;

  off_t image_size_bytes = static_cast<off_t>(num_mb) << 20;
  // The newfs_msdos tool with the mandatory -C option will do the same
  // as below to zero the image file, so we don't need to do it here
  if (image_fmt != "sdcard") {
    auto fd = SharedFD::Open(image, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd->Truncate(image_size_bytes) != 0) {
      LOG(ERROR) << "`truncate --size=" << num_mb << "M " << image
                 << "` failed:" << fd->StrError();
      return;
    }
  }

  if (image_fmt == "ext4") {
    execute({"/sbin/mkfs.ext4", image});
  } else if (image_fmt == "f2fs") {
    auto make_f2fs_path = cuttlefish::HostBinaryPath("make_f2fs");
    execute({make_f2fs_path, "-t", image_fmt, image, "-C", "utf8",
            "-O", "compression,extra_attr", "-g", "android"});
  } else if (image_fmt == "sdcard") {
    // Reserve 1MB in the image for the MBR and padding, to simulate what
    // other OSes do by default when partitioning a drive
    CHECK(NewfsMsdos(image, num_mb, 1) == true)
        << "Failed to create SD-Card filesystem";
    // Write the MBR after the filesystem is formatted, as the formatting tools
    // don't consistently preserve the image contents
    MasterBootRecord mbr = {
        .partitions = {{
            .partition_type = 0xC,
            .first_lba = (std::uint32_t)1 << 20 / SECTOR_SIZE,
            .num_sectors = (std::uint32_t)image_size_bytes / SECTOR_SIZE,
        }},
        .boot_signature = {0x55, 0xAA},
    };
    auto fd = SharedFD::Open(image, O_RDWR);
    if (WriteAllBinary(fd, &mbr) != sizeof(MasterBootRecord)) {
      LOG(ERROR) << "Writing MBR to " << image << " failed:" << fd->StrError();
      return;
    }
  } else if (image_fmt != "none") {
    LOG(WARNING) << "Unknown image format '" << image_fmt
                 << "' for " << image << ", treating as 'none'.";
  }
}

DataImageResult ApplyDataImagePolicy(const CuttlefishConfig& config,
                                     const std::string& data_image) {
  bool data_exists = FileHasContent(data_image.c_str());
  bool remove{};
  bool create{};
  bool resize{};

  if (config.data_policy() == kDataPolicyUseExisting) {
    if (!data_exists) {
      LOG(ERROR) << "Specified data image file does not exists: " << data_image;
      return DataImageResult::Error;
    }
    if (config.blank_data_image_mb() > 0) {
      LOG(ERROR) << "You should NOT use -blank_data_image_mb with -data_policy="
                 << kDataPolicyUseExisting;
      return DataImageResult::Error;
    }
    create = false;
    remove = false;
    resize = false;
  } else if (config.data_policy() == kDataPolicyAlwaysCreate) {
    remove = data_exists;
    create = true;
    resize = false;
  } else if (config.data_policy() == kDataPolicyCreateIfMissing) {
    create = !data_exists;
    remove = false;
    resize = false;
  } else if (config.data_policy() == kDataPolicyResizeUpTo) {
    create = false;
    remove = false;
    resize = true;
  } else {
    LOG(ERROR) << "Invalid data_policy: " << config.data_policy();
    return DataImageResult::Error;
  }

  if (remove) {
    RemoveFile(data_image.c_str());
  }

  if (create) {
    if (config.blank_data_image_mb() <= 0) {
      LOG(ERROR) << "-blank_data_image_mb is required to create data image";
      return DataImageResult::Error;
    }
    CreateBlankImage(data_image.c_str(), config.blank_data_image_mb(),
                     config.blank_data_image_fmt());
    return DataImageResult::FileUpdated;
  } else if (resize) {
    if (!data_exists) {
      LOG(ERROR) << data_image << " does not exist, but resizing was requested";
      return DataImageResult::Error;
    }
    bool success = ResizeImage(data_image.c_str(), config.blank_data_image_mb());
    return success ? DataImageResult::FileUpdated : DataImageResult::Error;
  } else {
    LOG(DEBUG) << data_image << " exists. Not creating it.";
    return DataImageResult::NoChange;
  }
}

bool InitializeMiscImage(const std::string& misc_image) {
  bool misc_exists = FileHasContent(misc_image.c_str());

  if (misc_exists) {
    LOG(DEBUG) << "misc partition image: use existing";
    return true;
  }

  LOG(DEBUG) << "misc partition image: creating empty";
  CreateBlankImage(misc_image, 1 /* mb */, "none");
  return true;
}

bool InitializeEspImage(const std::string& esp_image,
                        const std::string& kernel_path,
                        const std::string& initramfs_path) {
  bool esp_exists = FileHasContent(esp_image.c_str());
  if (esp_exists) {
    LOG(DEBUG) << "esp partition image: use existing";
    return true;
  }

  LOG(DEBUG) << "esp partition image: creating default";

  // newfs_msdos won't make a partition smaller than 257 mb
  // this should be enough for anybody..
  auto tmp_esp_image = esp_image + ".tmp";
  if (!NewfsMsdos(tmp_esp_image, 257 /* mb */, 0 /* mb (offset) */)) {
    LOG(ERROR) << "Failed to create filesystem for " << tmp_esp_image;
    return false;
  }

  // For licensing and build reproducibility reasons, pick up the bootloaders
  // from the host Linux distribution (if present) and pack them into the
  // automatically generated ESP. If the user wants their own bootloaders,
  // they can use -esp_image=/path/to/esp.img to override, so we don't need
  // to accommodate customizations of this packing process.

  // Currently we only support Debian based distributions, and GRUB is built
  // for those distros to always load grub.cfg from EFI/debian/grub.cfg, and
  // nowhere else. If you want to add support for other distros, make the
  // extra directories below and copy the initial grub.cfg there as well
  auto mmd = HostBinaryPath("mmd");
  auto success =
      execute({mmd, "-i", tmp_esp_image, "EFI", "EFI/BOOT", "EFI/debian"});
  if (success != 0) {
    LOG(ERROR) << "Failed to create directories in " << tmp_esp_image;
    return false;
  }

  // The grub binaries are small, so just copy all the architecture blobs
  // we can find, which minimizes complexity. If the user removed the grub bin
  // package from their system, the ESP will be empty and Other OS will not be
  // supported
  auto mcopy = HostBinaryPath("mcopy");
  bool copied = false;
  for (auto grub : kGrubBlobTable) {
    if (!FileExists(grub.first)) {
      continue;
    }
    success = execute(
        {mcopy, "-o", "-i", tmp_esp_image, "-s", grub.first, "::" + grub.second});
    if (success != 0) {
      LOG(ERROR) << "Failed to copy " << grub.first << " to " << grub.second
                 << " in " << tmp_esp_image;
      return false;
    }
    copied = true;
  }

  if (!copied) {
    LOG(ERROR) << "No GRUB binaries were found on this system; Other OS "
                  "support will be broken";
    return false;
  }

  auto grub_cfg = DefaultHostArtifactsPath("etc/grub/grub.cfg");
  CHECK(FileExists(grub_cfg)) << "Missing file " << grub_cfg << "!";
  success = execute({mcopy, "-i", tmp_esp_image, "-s", grub_cfg, "::EFI/debian/"});
  if (success != 0) {
    LOG(ERROR) << "Failed to copy " << grub_cfg << " to " << tmp_esp_image;
    return false;
  }

  if (!kernel_path.empty()) {
    success = execute({mcopy, "-i", tmp_esp_image, "-s", kernel_path, "::vmlinuz"});
    if (success != 0) {
      LOG(ERROR) << "Failed to copy " << kernel_path << " to " << tmp_esp_image;
      return false;
    }

    if (!initramfs_path.empty()) {
      success = execute(
          {mcopy, "-i", tmp_esp_image, "-s", initramfs_path, "::initrd.img"});
      if (success != 0) {
        LOG(ERROR) << "Failed to copy " << initramfs_path << " to "
                   << tmp_esp_image;
        return false;
      }
    }
  }

  CHECK(cuttlefish::RenameFile(tmp_esp_image, esp_image))
      << "Renaming " << tmp_esp_image << " to " << esp_image << " failed";
  return true;
}

} // namespace cuttlefish
