#include "host/libs/config/data_image.h"

#include <android-base/logging.h>
#include <android-base/result.h>

#include "blkid.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/mbr.h"
#include "host/libs/vm_manager/gem5_manager.h"

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
const std::string kM5 = "";

// These are the paths Debian installs the monoliths to. If another distro
// uses an alternative monolith path, add it to this table
const std::pair<std::string, std::string> kGrubBlobTable[] = {
    {"/usr/lib/grub/i386-efi/monolithic/grubia32.efi", kBootPathIA32},
    {"/usr/lib/grub/arm64-efi/monolithic/grubaa64.efi", kBootPathAA64},
};

// M5 checkpoint required binary file
const std::pair<std::string, std::string> kM5BlobTable[] = {
    {"/tmp/m5", kM5},
};

bool ForceFsckImage(const CuttlefishConfig& config,
                    const std::string& data_image) {
  std::string fsck_path;
  if (config.userdata_format() == "f2fs") {
    fsck_path = HostBinaryPath("fsck.f2fs");
  } else if (config.userdata_format() == "ext4") {
    fsck_path = "/sbin/e2fsck";
  }
  int fsck_status = execute({fsck_path, "-y", "-f", data_image});
  if (fsck_status & ~(FSCK_ERROR_CORRECTED|FSCK_ERROR_CORRECTED_REQUIRES_REBOOT)) {
    LOG(ERROR) << "`" << fsck_path << " -y -f " << data_image << "` failed with code "
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

bool ResizeImage(const CuttlefishConfig& config, const std::string& data_image,
                 int data_image_mb) {
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
    bool fsck_success = ForceFsckImage(config, data_image);
    if (!fsck_success) {
      return false;
    }
    std::string resize_path;
    if (config.userdata_format() == "f2fs") {
      resize_path = HostBinaryPath("resize.f2fs");
    } else if (config.userdata_format() == "ext4") {
      resize_path = "/sbin/resize2fs";
    }
    int resize_status = execute({resize_path, data_image});
    if (resize_status != 0) {
      LOG(ERROR) << "`" << resize_path << " " << data_image << "` failed with code "
                 << resize_status;
      return false;
    }
    fsck_success = ForceFsckImage(config, data_image);
    if (!fsck_success) {
      return false;
    }
  }
  return true;
}
} // namespace

bool CreateBlankImage(
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
      return false;
    }
  }

  if (image_fmt == "ext4") {
    if (execute({"/sbin/mkfs.ext4", image}) != 0) {
      return false;
    }
  } else if (image_fmt == "f2fs") {
    auto make_f2fs_path = cuttlefish::HostBinaryPath("make_f2fs");
    if (execute({make_f2fs_path, "-t", image_fmt, image, "-C", "utf8", "-O",
             "compression,extra_attr,project_quota", "-g", "android"}) != 0) {
      return false;
    }
  } else if (image_fmt == "sdcard") {
    // Reserve 1MB in the image for the MBR and padding, to simulate what
    // other OSes do by default when partitioning a drive
    off_t offset_size_bytes = 1 << 20;
    image_size_bytes -= offset_size_bytes;
    if (!NewfsMsdos(image, num_mb, 1)) {
      LOG(ERROR) << "Failed to create SD-Card filesystem";
      return false;
    }
    // Write the MBR after the filesystem is formatted, as the formatting tools
    // don't consistently preserve the image contents
    MasterBootRecord mbr = {
        .partitions = {{
            .partition_type = 0xC,
            .first_lba = (std::uint32_t) offset_size_bytes / SECTOR_SIZE,
            .num_sectors = (std::uint32_t) image_size_bytes / SECTOR_SIZE,
        }},
        .boot_signature = {0x55, 0xAA},
    };
    auto fd = SharedFD::Open(image, O_RDWR);
    if (WriteAllBinary(fd, &mbr) != sizeof(MasterBootRecord)) {
      LOG(ERROR) << "Writing MBR to " << image << " failed:" << fd->StrError();
      return false;
    }
  } else if (image_fmt != "none") {
    LOG(WARNING) << "Unknown image format '" << image_fmt
                 << "' for " << image << ", treating as 'none'.";
  }
  return true;
}

std::string GetFsType(const std::string& path) {
  std::string fs_type;
  blkid_cache cache;
  if (blkid_get_cache(&cache, NULL) < 0) {
    LOG(INFO) << "blkid_get_cache failed";
    return fs_type;
  }
  blkid_dev dev = blkid_get_dev(cache, path.c_str(), BLKID_DEV_NORMAL);
  if (!dev) {
    LOG(INFO) << "blkid_get_dev failed";
    blkid_put_cache(cache);
    return fs_type;
  }

  const char *type, *value;
  blkid_tag_iterate iter = blkid_tag_iterate_begin(dev);
  while (blkid_tag_next(iter, &type, &value) == 0) {
    if (!strcmp(type, "TYPE")) {
      fs_type = value;
    }
  }
  blkid_tag_iterate_end(iter);
  blkid_put_cache(cache);
  return fs_type;
}

struct DataImageTag {};

class FixedDataImagePath : public DataImagePath {
 public:
  INJECT(FixedDataImagePath(ANNOTATED(DataImageTag, std::string) path))
      : path_(path) {}

  const std::string& Path() const override { return path_; }

 private:
  std::string path_;
};

fruit::Component<DataImagePath> FixedDataImagePathComponent(
    const std::string* path) {
  return fruit::createComponent()
      .bind<DataImagePath, FixedDataImagePath>()
      .bindInstance<fruit::Annotated<DataImageTag, std::string>>(*path);
}

class InitializeDataImageImpl : public InitializeDataImage {
 public:
  INJECT(InitializeDataImageImpl(const CuttlefishConfig& config,
                                 DataImagePath& data_path))
      : config_(config), data_path_(data_path) {}

  // SetupFeature
  std::string Name() const override { return "InitializeDataImageImpl"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override {
    auto action = ChooseAction();
    if (!action.ok()) {
      LOG(ERROR) << "Failed to select a userdata processing action: "
                 << action.error();
      return false;
    }
    auto result = EvaluateAction(*action);
    if (!result.ok()) {
      LOG(ERROR) << "Failed to evaluate userdata action: " << result.error();
      return false;
    }
    return true;
  }

 private:
  enum class DataImageAction { kNoAction, kCreateImage, kResizeImage };

  Result<DataImageAction> ChooseAction() {
    if (config_.data_policy() == kDataPolicyAlwaysCreate) {
      return DataImageAction::kCreateImage;
    }
    if (!FileHasContent(data_path_.Path())) {
      if (config_.data_policy() == kDataPolicyUseExisting) {
        return CF_ERR("A data image must exist to use -data_policy="
                      << kDataPolicyUseExisting);
      } else if (config_.data_policy() == kDataPolicyResizeUpTo) {
        return CF_ERR(data_path_.Path()
                      << " does not exist, but resizing was requested");
      }
      return DataImageAction::kCreateImage;
    }
    if (GetFsType(data_path_.Path()) != config_.userdata_format()) {
      CF_EXPECT(config_.data_policy() == kDataPolicyResizeUpTo,
                "Changing the fs format is incompatible with -data_policy="
                    << kDataPolicyResizeUpTo);
      return DataImageAction::kCreateImage;
    }
    if (config_.data_policy() == kDataPolicyResizeUpTo) {
      return DataImageAction::kResizeImage;
    }
    return DataImageAction::kNoAction;
  }

  Result<void> EvaluateAction(DataImageAction action) {
    switch (action) {
      case DataImageAction::kNoAction:
        LOG(DEBUG) << data_path_.Path() << " exists. Not creating it.";
        return {};
      case DataImageAction::kCreateImage: {
        RemoveFile(data_path_.Path());
        CF_EXPECT(config_.blank_data_image_mb() != 0,
                  "Expected `-blank_data_image_mb` to be set for "
                  "image creation.");
        CF_EXPECT(
            CreateBlankImage(data_path_.Path(), config_.blank_data_image_mb(),
                             config_.userdata_format()),
            "Failed to create a blank image at \""
                << data_path_.Path() << "\" with size "
                << config_.blank_data_image_mb() << " and format \""
                << config_.userdata_format() << "\"");
        return {};
      }
      case DataImageAction::kResizeImage: {
        CF_EXPECT(config_.blank_data_image_mb() != 0,
                  "Expected `-blank_data_image_mb` to be set for "
                  "image resizing.");
        CF_EXPECT(ResizeImage(config_, data_path_.Path(),
                              config_.blank_data_image_mb()),
                  "Failed to resize \"" << data_path_.Path() << "\" to "
                                        << config_.blank_data_image_mb()
                                        << " MB");
        return {};
      }
    }
  }

  const CuttlefishConfig& config_;
  DataImagePath& data_path_;
};

fruit::Component<fruit::Required<const CuttlefishConfig, DataImagePath>,
                 InitializeDataImage>
InitializeDataImageComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, InitializeDataImage>()
      .bind<InitializeDataImage, InitializeDataImageImpl>();
}

struct MiscImageTag {};

class FixedMiscImagePath : public MiscImagePath {
 public:
  INJECT(FixedMiscImagePath(ANNOTATED(MiscImageTag, std::string) path))
      : path_(path) {}

  const std::string& Path() const override { return path_; }

 private:
  std::string path_;
};

class InitializeMiscImageImpl : public InitializeMiscImage {
 public:
  INJECT(InitializeMiscImageImpl(MiscImagePath& misc_path))
      : misc_path_(misc_path) {}

  // SetupFeature
  std::string Name() const override { return "InitializeMiscImageImpl"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override {
    bool misc_exists = FileHasContent(misc_path_.Path());

    if (misc_exists) {
      LOG(DEBUG) << "misc partition image: use existing at \""
                 << misc_path_.Path() << "\"";
      return true;
    }

    LOG(DEBUG) << "misc partition image: creating empty at \""
               << misc_path_.Path() << "\"";
    if (!CreateBlankImage(misc_path_.Path(), 1 /* mb */, "none")) {
      LOG(ERROR) << "Failed to create misc image";
      return false;
    }
    return true;
  }

 private:
  MiscImagePath& misc_path_;
};

fruit::Component<MiscImagePath> FixedMiscImagePathComponent(
    const std::string* path) {
  return fruit::createComponent()
      .bind<MiscImagePath, FixedMiscImagePath>()
      .bindInstance<fruit::Annotated<MiscImageTag, std::string>>(*path);
}

fruit::Component<fruit::Required<MiscImagePath>, InitializeMiscImage>
InitializeMiscImageComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, InitializeMiscImage>()
      .bind<InitializeMiscImage, InitializeMiscImageImpl>();
}

struct EspImageTag {};
struct KernelPathTag {};
struct InitRamFsTag {};
struct RootFsTag {};
struct ConfigTag {};

class InitializeEspImageImpl : public InitializeEspImage {
 public:
  INJECT(InitializeEspImageImpl(ANNOTATED(EspImageTag, std::string) esp_image,
                                ANNOTATED(KernelPathTag, std::string)
                                    kernel_path,
                                ANNOTATED(InitRamFsTag, std::string)
                                    initramfs_path,
                                ANNOTATED(RootFsTag, std::string) rootfs_path,
                                ANNOTATED(ConfigTag, const CuttlefishConfig *) config))
      : esp_image_(esp_image),
        kernel_path_(kernel_path),
        initramfs_path_(initramfs_path),
        rootfs_path_(rootfs_path),
        config_(config){}

  // SetupFeature
  std::string Name() const override { return "InitializeEspImageImpl"; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Enabled() const override { return !rootfs_path_.empty(); }

 protected:
  bool Setup() override {
    bool esp_exists = FileHasContent(esp_image_);
    if (esp_exists) {
      LOG(DEBUG) << "esp partition image: use existing";
      return true;
    }

    LOG(DEBUG) << "esp partition image: creating default";

    // newfs_msdos won't make a partition smaller than 257 mb
    // this should be enough for anybody..
    auto tmp_esp_image = esp_image_ + ".tmp";
    if (!NewfsMsdos(tmp_esp_image, 257 /* mb */, 0 /* mb (offset) */)) {
      LOG(ERROR) << "Failed to create filesystem for " << tmp_esp_image;
      return false;
    }

    // For licensing and build reproducibility reasons, pick up the bootloaders
    // from the host Linux distribution (if present) and pack them into the
    // automatically generated ESP. If the user wants their own bootloaders,
    // they can use -esp_image=/path/to/esp.img to override, so we don't need
    // to accommodate customizations of this packing process.

    int success;
    const std::pair<std::string, std::string> *kBlobTable;
    std::size_t size;
    // Skip GRUB on Gem5
    if (config_->vm_manager() != vm_manager::Gem5Manager::name()){
      // Currently we only support Debian based distributions, and GRUB is built
      // for those distros to always load grub.cfg from EFI/debian/grub.cfg, and
      // nowhere else. If you want to add support for other distros, make the
      // extra directories below and copy the initial grub.cfg there as well
      auto mmd = HostBinaryPath("mmd");
      success =
          execute({mmd, "-i", tmp_esp_image, "EFI", "EFI/BOOT", "EFI/debian"});
      if (success != 0) {
        LOG(ERROR) << "Failed to create directories in " << tmp_esp_image;
        return false;
      }
      size = sizeof(kGrubBlobTable)/sizeof(const std::pair<std::string, std::string>);
      kBlobTable = kGrubBlobTable;
    } else {
      size = sizeof(kM5BlobTable)/sizeof(const std::pair<std::string, std::string>);
      kBlobTable = kM5BlobTable;
    }

    // The grub binaries are small, so just copy all the architecture blobs
    // we can find, which minimizes complexity. If the user removed the grub bin
    // package from their system, the ESP will be empty and Other OS will not be
    // supported
    auto mcopy = HostBinaryPath("mcopy");
    bool copied = false;
    for (int i=0; i<size; i++) {
      auto grub = kBlobTable[i];
      if (!FileExists(grub.first)) {
        continue;
      }
      success = execute({mcopy, "-o", "-i", tmp_esp_image, "-s", grub.first,
                         "::" + grub.second});
      if (success != 0) {
        LOG(ERROR) << "Failed to copy " << grub.first << " to " << grub.second
                   << " in " << tmp_esp_image;
        return false;
      }
      copied = true;
    }

    if (!copied) {
      LOG(ERROR) << "Binary dependencies were not found on this system; Other OS "
                    "support will be broken";
      return false;
    }

    // Skip Gem5 case. Gem5 will never be able to use bootloaders like grub.
    if (config_->vm_manager() != vm_manager::Gem5Manager::name()){
      auto grub_cfg = DefaultHostArtifactsPath("etc/grub/grub.cfg");
      CHECK(FileExists(grub_cfg)) << "Missing file " << grub_cfg << "!";
      success =
          execute({mcopy, "-i", tmp_esp_image, "-s", grub_cfg, "::EFI/debian/"});
      if (success != 0) {
        LOG(ERROR) << "Failed to copy " << grub_cfg << " to " << tmp_esp_image;
        return false;
      }
    }

    if (!kernel_path_.empty()) {
      success = execute(
          {mcopy, "-i", tmp_esp_image, "-s", kernel_path_, "::vmlinuz"});
      if (success != 0) {
        LOG(ERROR) << "Failed to copy " << kernel_path_ << " to "
                   << tmp_esp_image;
        return false;
      }

      if (!initramfs_path_.empty()) {
        success = execute({mcopy, "-i", tmp_esp_image, "-s", initramfs_path_,
                           "::initrd.img"});
        if (success != 0) {
          LOG(ERROR) << "Failed to copy " << initramfs_path_ << " to "
                     << tmp_esp_image;
          return false;
        }
      }
    }

    if (!cuttlefish::RenameFile(tmp_esp_image, esp_image_)) {
      LOG(ERROR) << "Renaming " << tmp_esp_image << " to " << esp_image_
                 << " failed";
      return false;
    }
    return true;
  }

 private:
  std::string esp_image_;
  std::string kernel_path_;
  std::string initramfs_path_;
  std::string rootfs_path_;
  const CuttlefishConfig* config_;
};

fruit::Component<fruit::Required<const CuttlefishConfig>,
    InitializeEspImage> InitializeEspImageComponent(
    const std::string* esp_image, const std::string* kernel_path,
    const std::string* initramfs_path, const std::string* rootfs_path,
    const CuttlefishConfig* config) {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, InitializeEspImage>()
      .bind<InitializeEspImage, InitializeEspImageImpl>()
      .bindInstance<fruit::Annotated<EspImageTag, std::string>>(*esp_image)
      .bindInstance<fruit::Annotated<KernelPathTag, std::string>>(*kernel_path)
      .bindInstance<fruit::Annotated<InitRamFsTag, std::string>>(
          *initramfs_path)
      .bindInstance<fruit::Annotated<RootFsTag, std::string>>(*rootfs_path)
      .bindInstance<fruit::Annotated<ConfigTag, CuttlefishConfig>>(*config);
}

} // namespace cuttlefish
