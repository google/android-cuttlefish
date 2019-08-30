#include "host/commands/launch/data_image.h"

#include <glog/logging.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"

namespace {
const std::string kDataPolicyUseExisting = "use_existing";
const std::string kDataPolicyCreateIfMissing = "create_if_missing";
const std::string kDataPolicyAlwaysCreate = "always_create";
const std::string kDataPolicyResizeUpTo= "resize_up_to";

const int FSCK_ERROR_CORRECTED = 1;
const int FSCK_ERROR_CORRECTED_REQUIRES_REBOOT = 2;

bool ForceFsckImage(const char* data_image) {
  int fsck_status = cvd::execute({"/sbin/e2fsck", "-y", "-f", data_image});
  if (fsck_status & ~(FSCK_ERROR_CORRECTED|FSCK_ERROR_CORRECTED_REQUIRES_REBOOT)) {
    LOG(ERROR) << "`e2fsck -y -f " << data_image << "` failed with code "
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
    int truncate_status =
        cvd::SharedFD::Open(data_image, O_RDWR)->Truncate(raw_target);
    if (truncate_status != 0) {
      LOG(ERROR) << "`truncate --size=" << data_image_mb << "M "
                  << data_image << "` failed with code " << truncate_status;
      return false;
    }
    bool fsck_success = ForceFsckImage(data_image);
    if (!fsck_success) {
      return false;
    }
    int resize_status = cvd::execute({"/sbin/resize2fs", data_image});
    if (resize_status != 0) {
      LOG(ERROR) << "`resize2fs " << data_image << "` failed with code "
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
    const std::string& image, int image_mb, const std::string& image_fmt) {
  LOG(INFO) << "Creating " << image;
  std::string of = "of=";
  of += image;
  std::string count = "count=";
  count += std::to_string(image_mb);
  cvd::execute({"/bin/dd", "if=/dev/zero", of, "bs=1M", count});
  if (image_fmt != "none") {
    cvd::execute({"/sbin/mkfs", "-t", image_fmt, image}, {"PATH=/sbin"});
  }
}

bool ApplyDataImagePolicy(const vsoc::CuttlefishConfig& config,
                          const std::string& data_image) {
  bool data_exists = cvd::FileHasContent(data_image.c_str());
  bool remove{};
  bool create{};
  bool resize{};

  if (config.data_policy() == kDataPolicyUseExisting) {
    if (!data_exists) {
      LOG(ERROR) << "Specified data image file does not exists: " << data_image;
      return false;
    }
    if (config.blank_data_image_mb() > 0) {
      LOG(ERROR) << "You should NOT use -blank_data_image_mb with -data_policy="
                 << kDataPolicyUseExisting;
      return false;
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
    return false;
  }

  if (remove) {
    cvd::RemoveFile(data_image.c_str());
  }

  if (create) {
    if (config.blank_data_image_mb() <= 0) {
      LOG(ERROR) << "-blank_data_image_mb is required to create data image";
      return false;
    }
    CreateBlankImage(data_image.c_str(), config.blank_data_image_mb(),
                     config.blank_data_image_fmt());
  } else if (resize) {
    if (!data_exists) {
      LOG(ERROR) << data_image << " does not exist, but resizing was requested";
      return false;
    }
    return ResizeImage(data_image.c_str(), config.blank_data_image_mb());
  } else {
    LOG(INFO) << data_image << " exists. Not creating it.";
  }

  return true;
}

bool InitializeMiscImage(const std::string& misc_image) {
  bool misc_exists = cvd::FileHasContent(misc_image.c_str());

  if (misc_exists) {
    LOG(INFO) << "misc partition image: use existing";
    return true;
  }

  LOG(INFO) << "misc partition image: creating empty";
  CreateBlankImage(misc_image, 1 /* mb */, "none");
  return true;
}
