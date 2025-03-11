#include "host/libs/config/data_image.h"

#include <android-base/logging.h>
#include <android-base/result.h>

#include "blkid.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/mbr.h"
#include "host/libs/config/esp.h"
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
// These are the paths Debian installs the monoliths to. If another distro
// uses an alternative monolith path, add it to this table
const std::string kBootSrcPathIA32 = "/usr/lib/grub/i386-efi/monolithic/grubia32.efi";
const std::string kBootDestPathIA32 = "EFI/BOOT/BOOTIA32.EFI";

const std::string kBootSrcPathAA64 = "/usr/lib/grub/arm64-efi/monolithic/grubaa64.efi";
const std::string kBootDestPathAA64 = "EFI/BOOT/BOOTAA64.EFI";

const std::string kModulesDestPath = "EFI/modules";
const std::string kMultibootModuleSrcPathIA32 = "/usr/lib/grub/i386-efi/multiboot.mod";
const std::string kMultibootModuleDestPathIA32 = kModulesDestPath + "/multiboot.mod";

const std::string kMultibootModuleSrcPathAA64 = "/usr/lib/grub/arm64-efi/multiboot.mod";
const std::string kMultibootModuleDestPathAA64 = kModulesDestPath + "/multiboot.mod";

bool ForceFsckImage(const std::string& data_image,
                    const CuttlefishConfig::InstanceSpecific& instance) {
  std::string fsck_path;
  if (instance.userdata_format() == "f2fs") {
    fsck_path = HostBinaryPath("fsck.f2fs");
  } else if (instance.userdata_format() == "ext4") {
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

bool ResizeImage(const std::string& data_image, int data_image_mb,
                 const CuttlefishConfig::InstanceSpecific& instance) {
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
    bool fsck_success = ForceFsckImage(data_image, instance);
    if (!fsck_success) {
      return false;
    }
    std::string resize_path;
    if (instance.userdata_format() == "f2fs") {
      resize_path = HostBinaryPath("resize.f2fs");
    } else if (instance.userdata_format() == "ext4") {
      resize_path = "/sbin/resize2fs";
    }
    int resize_status = execute({resize_path, data_image});
    if (resize_status != 0) {
      LOG(ERROR) << "`" << resize_path << " " << data_image << "` failed with code "
                 << resize_status;
      return false;
    }
    fsck_success = ForceFsckImage(data_image, instance);
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
    if (execute({make_f2fs_path, "-l", "data", image, "-C", "utf8", "-O",
     "compression,extra_attr,project_quota,casefold", "-g", "android"}) != 0) {
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

class InitializeDataImageImpl : public InitializeDataImage {
 public:
  INJECT(InitializeDataImageImpl(
      const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeDataImageImpl"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override {
    auto action = ChooseAction();
    if (!action.ok()) {
      LOG(ERROR) << "Failed to select a userdata processing action: "
                 << action.error().Message();
      LOG(DEBUG) << "Failed to select a userdata processing action: "
                 << action.error().Trace();
      return false;
    }
    auto result = EvaluateAction(*action);
    if (!result.ok()) {
      LOG(ERROR) << "Failed to evaluate userdata action: "
                 << result.error().Message();
      LOG(DEBUG) << "Failed to evaluate userdata action: "
                 << result.error().Trace();
      return false;
    }
    return true;
  }

 private:
  enum class DataImageAction { kNoAction, kCreateImage, kResizeImage };

  Result<DataImageAction> ChooseAction() {
    if (instance_.data_policy() == kDataPolicyAlwaysCreate) {
      return DataImageAction::kCreateImage;
    }
    if (!FileHasContent(instance_.data_image())) {
      if (instance_.data_policy() == kDataPolicyUseExisting) {
        return CF_ERR("A data image must exist to use -data_policy="
                      << kDataPolicyUseExisting);
      } else if (instance_.data_policy() == kDataPolicyResizeUpTo) {
        return CF_ERR(instance_.data_image()
                      << " does not exist, but resizing was requested");
      }
      return DataImageAction::kCreateImage;
    }
    if (instance_.data_policy() == kDataPolicyUseExisting) {
      return DataImageAction::kNoAction;
    }
    auto current_fs_type = GetFsType(instance_.data_image());
    if (current_fs_type != instance_.userdata_format()) {
      CF_EXPECT(instance_.data_policy() == kDataPolicyResizeUpTo,
                "Changing the fs format is incompatible with -data_policy="
                    << kDataPolicyResizeUpTo << " (\"" << current_fs_type
                    << "\" != \"" << instance_.userdata_format() << "\")");
      return DataImageAction::kCreateImage;
    }
    if (instance_.data_policy() == kDataPolicyResizeUpTo) {
      return DataImageAction::kResizeImage;
    }
    return DataImageAction::kNoAction;
  }

  Result<void> EvaluateAction(DataImageAction action) {
    switch (action) {
      case DataImageAction::kNoAction:
        LOG(DEBUG) << instance_.data_image() << " exists. Not creating it.";
        return {};
      case DataImageAction::kCreateImage: {
        RemoveFile(instance_.data_image());
        CF_EXPECT(instance_.blank_data_image_mb() != 0,
                  "Expected `-blank_data_image_mb` to be set for "
                  "image creation.");
        CF_EXPECT(CreateBlankImage(instance_.data_image(),
                                   instance_.blank_data_image_mb(),
                                   instance_.userdata_format()),
                  "Failed to create a blank image at \""
                      << instance_.data_image() << "\" with size "
                      << instance_.blank_data_image_mb() << " and format \""
                      << instance_.userdata_format() << "\"");
        return {};
      }
      case DataImageAction::kResizeImage: {
        CF_EXPECT(instance_.blank_data_image_mb() != 0,
                  "Expected `-blank_data_image_mb` to be set for "
                  "image resizing.");
        CF_EXPECT(ResizeImage(instance_.data_image(),
                              instance_.blank_data_image_mb(), instance_),
                  "Failed to resize \"" << instance_.data_image() << "\" to "
                                        << instance_.blank_data_image_mb()
                                        << " MB");
        return {};
      }
    }
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
};

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InitializeDataImage>
InitializeDataImageComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, InitializeDataImage>()
      .bind<InitializeDataImage, InitializeDataImageImpl>();
}

class InitializeMiscImageImpl : public InitializeMiscImage {
 public:
  INJECT(InitializeMiscImageImpl(
      const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeMiscImageImpl"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override {
    bool misc_exists = FileHasContent(instance_.misc_image());

    if (misc_exists) {
      LOG(DEBUG) << "misc partition image: use existing at \""
                 << instance_.misc_image() << "\"";
      return true;
    }

    LOG(DEBUG) << "misc partition image: creating empty at \""
               << instance_.misc_image() << "\"";
    if (!CreateBlankImage(instance_.new_misc_image(), 1 /* mb */, "none")) {
      LOG(ERROR) << "Failed to create misc image";
      return false;
    }
    return true;
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
};

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InitializeMiscImage>
InitializeMiscImageComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, InitializeMiscImage>()
      .bind<InitializeMiscImage, InitializeMiscImageImpl>();
}

class InitializeEspImageImpl : public InitializeEspImage {
 public:
  INJECT(InitializeEspImageImpl(
      const CuttlefishConfig& config,
      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // SetupFeature
  std::string Name() const override { return "InitializeEspImageImpl"; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Enabled() const override {
    const auto flow = instance_.boot_flow();
    const auto vm = config_.vm_manager();
    const auto not_gem5 = vm != vm_manager::Gem5Manager::name();
    const auto boot_flow_required_esp =
        flow == CuttlefishConfig::InstanceSpecific::BootFlow::Linux ||
        flow == CuttlefishConfig::InstanceSpecific::BootFlow::Fuchsia;

    return not_gem5 && boot_flow_required_esp;
  }

 protected:
  bool Setup() override {
    LOG(DEBUG) << "esp partition image: creating default";
    auto builder = EspBuilder(instance_.otheros_esp_image());

    // For licensing and build reproducibility reasons, pick up the bootloaders
    // from the host Linux distribution (if present) and pack them into the
    // automatically generated ESP. If the user wants their own bootloaders,
    // they can use -esp_image=/path/to/esp.img to override, so we don't need
    // to accommodate customizations of this packing process.

    // Currently we only support Debian based distributions, and GRUB is built
    // for those distros to always load grub.cfg from EFI/debian/grub.cfg, and
    // nowhere else. If you want to add support for other distros, make the
    // extra directories below and copy the initial grub.cfg there as well
    builder.Directory("EFI")
        .Directory("EFI/BOOT")
        .Directory("EFI/debian")
        .Directory("EFI/modules");

    const auto flow = instance_.boot_flow();
    if (flow == CuttlefishConfig::InstanceSpecific::BootFlow::Linux ||
        flow == CuttlefishConfig::InstanceSpecific::BootFlow::Fuchsia) {
      auto grub_cfg = DefaultHostArtifactsPath("etc/grub/grub.cfg");
      builder.File(grub_cfg, "EFI/debian/grub.cfg", /* required */ true);
      switch (instance_.target_arch()) {
        case Arch::Arm:
        case Arch::Arm64:
          builder.File(kBootSrcPathAA64, kBootDestPathAA64, /* required */ true);
          builder.File(kMultibootModuleSrcPathAA64, kMultibootModuleDestPathAA64,
                        /* required */ true);
          break;
        case Arch::X86:
        case Arch::X86_64:
          builder.File(kBootSrcPathIA32, kBootDestPathIA32, /* required */ true);
          builder.File(kMultibootModuleSrcPathIA32, kMultibootModuleDestPathIA32,
                        /* required */ true);
          break;
      }
    }

    switch (flow) {
      case CuttlefishConfig::InstanceSpecific::BootFlow::Linux:
        builder.File(instance_.linux_kernel_path(), "vmlinuz", /* required */ true);
        if (!instance_.linux_initramfs_path().empty()) {
          builder.File(instance_.linux_initramfs_path(), "initrd.img", /* required */ true);
        }
        break;
      case CuttlefishConfig::InstanceSpecific::BootFlow::Fuchsia:
        builder.File(instance_.fuchsia_zedboot_path(), "zedboot.zbi",
                     /* required */ true);
        builder.File(instance_.fuchsia_multiboot_bin_path(), "multiboot.bin",
                     /* required */ true);
        break;
      default:
        break;
    }

    return builder.Build();
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 InitializeEspImage>
InitializeEspImageComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, InitializeEspImage>()
      .bind<InitializeEspImage, InitializeEspImageImpl>();
}

} // namespace cuttlefish
