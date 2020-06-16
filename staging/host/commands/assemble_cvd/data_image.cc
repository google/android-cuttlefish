#include "host/commands/assemble_cvd/data_image.h"

#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"

#include "host/commands/assemble_cvd/mbr.h"

namespace {
const std::string kDataPolicyUseExisting = "use_existing";
const std::string kDataPolicyCreateIfMissing = "create_if_missing";
const std::string kDataPolicyAlwaysCreate = "always_create";
const std::string kDataPolicyResizeUpTo= "resize_up_to";

const int FSCK_ERROR_CORRECTED = 1;
const int FSCK_ERROR_CORRECTED_REQUIRES_REBOOT = 2;

bool ForceFsckImage(const char* data_image) {
  auto fsck_path = vsoc::DefaultHostArtifactsPath("bin/fsck.f2fs");
  int fsck_status = cvd::execute({fsck_path, "-y", "-f", data_image});
  if (fsck_status & ~(FSCK_ERROR_CORRECTED|FSCK_ERROR_CORRECTED_REQUIRES_REBOOT)) {
    LOG(ERROR) << "`fsck.f2fs -y -f " << data_image << "` failed with code "
               << fsck_status;
    return false;
  }
  return true;
}

bool ResizeImage(const char* data_image, int data_image_mb) {
  auto file_mb = cvd::FileSize(data_image) >> 20;
  if (file_mb > data_image_mb) {
    LOG(ERROR) << data_image << " is already " << file_mb << " MB, will not "
               << "resize down.";
    return false;
  } else if (file_mb == data_image_mb) {
    LOG(INFO) << data_image << " is already the right size";
    return true;
  } else {
    off_t raw_target = static_cast<off_t>(data_image_mb) << 20;
    auto fd = cvd::SharedFD::Open(data_image, O_RDWR);
    if (fd->Truncate(raw_target) != 0) {
      LOG(ERROR) << "`truncate --size=" << data_image_mb << "M "
                  << data_image << "` failed:" << fd->StrError();
      return false;
    }
    bool fsck_success = ForceFsckImage(data_image);
    if (!fsck_success) {
      return false;
    }
    auto resize_path = vsoc::DefaultHostArtifactsPath("bin/resize.f2fs");
    int resize_status = cvd::execute({resize_path, data_image});
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
    auto fd = cvd::SharedFD::Open(image, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd->Truncate(image_size_bytes) != 0) {
      LOG(ERROR) << "`truncate --size=" << num_mb << "M " << image
                 << "` failed:" << fd->StrError();
      return;
    }
  }

  if (image_fmt == "ext4") {
    cvd::execute({"/sbin/mkfs.ext4", image});
  } else if (image_fmt == "f2fs") {
    auto make_f2fs_path = vsoc::DefaultHostArtifactsPath("bin/make_f2fs");
    cvd::execute({make_f2fs_path, "-t", image_fmt, image, "-g", "android"});
  } else if (image_fmt == "sdcard") {
    // Reserve 1MB in the image for the MBR and padding, to simulate what
    // other OSes do by default when partitioning a drive
    off_t offset_size_bytes = 1 << 20;
    image_size_bytes -= offset_size_bytes;
    off_t image_size_sectors = image_size_bytes / 512;
    auto newfs_msdos_path = vsoc::DefaultHostArtifactsPath("bin/newfs_msdos");
    cvd::execute({newfs_msdos_path, "-F", "32", "-m", "0xf8", "-a", "4088",
                                    "-o", "0",  "-c", "8",    "-h", "255",
                                    "-u", "63", "-S", "512",
                                    "-s", std::to_string(image_size_sectors),
                                    "-C", std::to_string(num_mb) + "M",
                                    "-@", std::to_string(offset_size_bytes),
                                    image});
    // Write the MBR after the filesystem is formatted, as the formatting tools
    // don't consistently preserve the image contents
    MasterBootRecord mbr = {
      .partitions = {{
        .partition_type = 0xC,
        .first_lba = (std::uint32_t) offset_size_bytes / SECTOR_SIZE,
        .num_sectors = (std::uint32_t) image_size_bytes / SECTOR_SIZE,
      }},
      .boot_signature = { 0x55, 0xAA },
    };
    auto fd = cvd::SharedFD::Open(image, O_RDWR);
    if (cvd::WriteAllBinary(fd, &mbr) != sizeof(MasterBootRecord)) {
      LOG(ERROR) << "Writing MBR to " << image << " failed:" << fd->StrError();
      return;
    }
  } else if (image_fmt != "none") {
    LOG(WARNING) << "Unknown image format '" << image_fmt
                 << "' for " << image << ", treating as 'none'.";
  }
}

DataImageResult ApplyDataImagePolicy(const vsoc::CuttlefishConfig& config,
                                     const std::string& data_image) {
  bool data_exists = cvd::FileHasContent(data_image.c_str());
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
    cvd::RemoveFile(data_image.c_str());
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
  bool misc_exists = cvd::FileHasContent(misc_image.c_str());

  if (misc_exists) {
    LOG(DEBUG) << "misc partition image: use existing";
    return true;
  }

  LOG(DEBUG) << "misc partition image: creating empty";
  CreateBlankImage(misc_image, 1 /* mb */, "none");
  return true;
}
