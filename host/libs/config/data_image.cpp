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
#include "host/libs/config/data_image.h"

#include <android-base/logging.h>
#include <android-base/result.h>

#include "blkid.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/esp.h"
#include "host/libs/config/mbr.h"
#include "host/libs/config/openwrt_args.h"
#include "host/libs/vm_manager/gem5_manager.h"

namespace cuttlefish {

namespace {

static constexpr std::string_view kDataPolicyUseExisting = "use_existing";
static constexpr std::string_view kDataPolicyAlwaysCreate = "always_create";
static constexpr std::string_view kDataPolicyResizeUpTo = "resize_up_to";

const int FSCK_ERROR_CORRECTED = 1;
const int FSCK_ERROR_CORRECTED_REQUIRES_REBOOT = 2;

Result<void> ForceFsckImage(
    const std::string& data_image,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::string fsck_path;
  if (instance.userdata_format() == "f2fs") {
    fsck_path = HostBinaryPath("fsck.f2fs");
  } else if (instance.userdata_format() == "ext4") {
    fsck_path = "/sbin/e2fsck";
  }
  int fsck_status = Execute({fsck_path, "-y", "-f", data_image});
  CF_EXPECTF(!(fsck_status &
               ~(FSCK_ERROR_CORRECTED | FSCK_ERROR_CORRECTED_REQUIRES_REBOOT)),
             "`{} -y -f {}` failed with code {}", fsck_path, data_image,
             fsck_status);
  return {};
}

Result<void> ResizeImage(const std::string& data_image, int data_image_mb,
                         const CuttlefishConfig::InstanceSpecific& instance) {
  auto file_mb = FileSize(data_image) >> 20;
  CF_EXPECTF(data_image_mb <= file_mb, "'{}' is already {} MB, won't downsize",
             data_image, file_mb);
  if (file_mb == data_image_mb) {
    LOG(INFO) << data_image << " is already the right size";
    return {};
  }
  off_t raw_target = static_cast<off_t>(data_image_mb) << 20;
  auto fd = SharedFD::Open(data_image, O_RDWR);
  CF_EXPECTF(fd->IsOpen(), "Can't open '{}': '{}'", data_image, fd->StrError());
  CF_EXPECTF(fd->Truncate(raw_target) == 0, "`truncate --size={}M {} fail: {}",
             data_image_mb, data_image, fd->StrError());
  CF_EXPECT(ForceFsckImage(data_image, instance));
  std::string resize_path;
  if (instance.userdata_format() == "f2fs") {
    resize_path = HostBinaryPath("resize.f2fs");
  } else if (instance.userdata_format() == "ext4") {
    resize_path = "/sbin/resize2fs";
  }
  if (resize_path != "") {
    CF_EXPECT_EQ(Execute({resize_path, data_image}), 0,
                 "`" << resize_path << " " << data_image << "` failed");
    CF_EXPECT(ForceFsckImage(data_image, instance));
  }

  return {};
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

enum class DataImageAction { kNoAction, kCreateImage, kResizeImage };

static Result<DataImageAction> ChooseDataImageAction(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (instance.data_policy() == kDataPolicyAlwaysCreate) {
    return DataImageAction::kCreateImage;
  }
  if (!FileHasContent(instance.data_image())) {
    if (instance.data_policy() == kDataPolicyUseExisting) {
      return CF_ERR("A data image must exist to use -data_policy="
                    << kDataPolicyUseExisting);
    } else if (instance.data_policy() == kDataPolicyResizeUpTo) {
      return CF_ERR(instance.data_image()
                    << " does not exist, but resizing was requested");
    }
    return DataImageAction::kCreateImage;
  }
  if (instance.data_policy() == kDataPolicyUseExisting) {
    return DataImageAction::kNoAction;
  }
  auto current_fs_type = GetFsType(instance.data_image());
  if (current_fs_type != instance.userdata_format()) {
    CF_EXPECT(instance.data_policy() != kDataPolicyResizeUpTo,
              "Changing the fs format is incompatible with -data_policy="
                  << kDataPolicyResizeUpTo << " (\"" << current_fs_type
                  << "\" != \"" << instance.userdata_format() << "\")");
    return DataImageAction::kCreateImage;
  }
  if (instance.data_policy() == kDataPolicyResizeUpTo) {
    return DataImageAction::kResizeImage;
  }
  return DataImageAction::kNoAction;
}

} // namespace

Result<void> CreateBlankImage(const std::string& image, int num_mb,
                              const std::string& image_fmt) {
  LOG(DEBUG) << "Creating " << image;

  off_t image_size_bytes = static_cast<off_t>(num_mb) << 20;
  // The newfs_msdos tool with the mandatory -C option will do the same
  // as below to zero the image file, so we don't need to do it here
  if (image_fmt != "sdcard") {
    auto fd = SharedFD::Open(image, O_CREAT | O_TRUNC | O_RDWR, 0666);
    CF_EXPECTF(fd->Truncate(image_size_bytes) == 0,
               "`truncate --size={}M '{}'` failed: {}", num_mb, image,
               fd->StrError());
  }

  if (image_fmt == "ext4") {
    CF_EXPECT(Execute({"/sbin/mkfs.ext4", image}) == 0);
  } else if (image_fmt == "f2fs") {
    auto make_f2fs_path = HostBinaryPath("make_f2fs");
    CF_EXPECT(
        Execute({make_f2fs_path, "-l", "data", image, "-C", "utf8", "-O",
                 "compression,extra_attr,project_quota,casefold", "-g",
                 "android", "-b", F2FS_BLOCKSIZE, "-w", F2FS_BLOCKSIZE}) == 0);
  } else if (image_fmt == "sdcard") {
    // Reserve 1MB in the image for the MBR and padding, to simulate what
    // other OSes do by default when partitioning a drive
    off_t offset_size_bytes = 1 << 20;
    image_size_bytes -= offset_size_bytes;
    CF_EXPECT(NewfsMsdos(image, num_mb, 1), "Failed to create SD-Card fs");
    // Write the MBR after the filesystem is formatted, as the formatting tools
    // don't consistently preserve the image contents
    MasterBootRecord mbr = {
        .partitions = {{
            .partition_type = 0xC,
            .first_lba = (std::uint32_t)offset_size_bytes / kSectorSize,
            .num_sectors = (std::uint32_t)image_size_bytes / kSectorSize,
        }},
        .boot_signature = {0x55, 0xAA},
    };
    auto fd = SharedFD::Open(image, O_RDWR);
    CF_EXPECTF(WriteAllBinary(fd, &mbr) == sizeof(MasterBootRecord),
               "Writing MBR to '{}' failed: '{}'", image, fd->StrError());
  } else if (image_fmt != "none") {
    LOG(WARNING) << "Unknown image format '" << image_fmt
                 << "' for " << image << ", treating as 'none'.";
  }
  return {};
}

Result<void> InitializeDataImage(
    const CuttlefishConfig::InstanceSpecific& instance) {
  auto action = CF_EXPECT(ChooseDataImageAction(instance));
  switch (action) {
    case DataImageAction::kNoAction:
      LOG(DEBUG) << instance.data_image() << " exists. Not creating it.";
      return {};
    case DataImageAction::kCreateImage: {
      RemoveFile(instance.new_data_image());
      CF_EXPECT(instance.blank_data_image_mb() != 0,
                "Expected `-blank_data_image_mb` to be set for "
                "image creation.");
      CF_EXPECT(CreateBlankImage(instance.new_data_image(),
                                 instance.blank_data_image_mb(),
                                 instance.userdata_format()),
                "Failed to create a blank image at \""
                    << instance.new_data_image() << "\" with size "
                    << instance.blank_data_image_mb() << " and format \""
                    << instance.userdata_format() << "\"");
      return {};
    }
    case DataImageAction::kResizeImage: {
      CF_EXPECT(instance.blank_data_image_mb() != 0,
                "Expected `-blank_data_image_mb` to be set for "
                "image resizing.");
      CF_EXPECTF(Copy(instance.data_image(), instance.new_data_image()),
                 "Failed to `cp {} {}`", instance.data_image(),
                 instance.new_data_image());
      CF_EXPECT(ResizeImage(instance.new_data_image(),
                            instance.blank_data_image_mb(), instance),
                "Failed to resize \"" << instance.new_data_image() << "\" to "
                                      << instance.blank_data_image_mb()
                                      << " MB");
      return {};
    }
  }
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
  Result<void> ResultSetup() override {
    if (FileHasContent(instance_.misc_image())) {
      LOG(DEBUG) << "misc partition image already exists";
      return {};
    }

    LOG(DEBUG) << "misc partition image: creating empty at \""
               << instance_.misc_image() << "\"";
    CF_EXPECT(CreateBlankImage(instance_.misc_image(), 1 /* mb */, "none"),
              "Failed to create misc image");
    return {};
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
    return EspRequiredForBootFlow() || EspRequiredForAPBootFlow();
  }

 protected:
  Result<void> ResultSetup() override {
    if (EspRequiredForAPBootFlow()) {
      LOG(DEBUG) << "creating esp_image: " << instance_.ap_esp_image_path();
      CF_EXPECT(BuildAPImage());
    }
    const auto is_not_gem5 = config_.vm_manager() != vm_manager::Gem5Manager::name();
    const auto esp_required_for_boot_flow = EspRequiredForBootFlow();
    if (is_not_gem5 && esp_required_for_boot_flow) {
      LOG(DEBUG) << "creating esp_image: " << instance_.esp_image_path();
      CF_EXPECT(BuildOSImage());
    }
    return {};
  }

 private:

  bool EspRequiredForBootFlow() const {
    const auto flow = instance_.boot_flow();
    using BootFlow = CuttlefishConfig::InstanceSpecific::BootFlow;
    return flow == BootFlow::AndroidEfiLoader || flow == BootFlow::ChromeOs ||
           flow == BootFlow::Linux || flow == BootFlow::Fuchsia;
  }

  bool EspRequiredForAPBootFlow() const {
    return instance_.ap_boot_flow() == CuttlefishConfig::InstanceSpecific::APBootFlow::Grub;
  }

  bool BuildAPImage() {
    auto linux = LinuxEspBuilder(instance_.ap_esp_image_path());
    InitLinuxArgs(linux);

    auto openwrt_args = OpenwrtArgsFromConfig(instance_);
    for (auto& openwrt_arg : openwrt_args) {
      linux.Argument(openwrt_arg.first, openwrt_arg.second);
    }

    linux.Root("/dev/vda2")
         .Architecture(instance_.target_arch())
         .Kernel(config_.ap_kernel_image());

    return linux.Build();
  }

  bool BuildOSImage() {
    switch (instance_.boot_flow()) {
      case CuttlefishConfig::InstanceSpecific::BootFlow::AndroidEfiLoader: {
        auto android_efi_loader =
            AndroidEfiLoaderEspBuilder(instance_.esp_image_path());
        android_efi_loader.EfiLoaderPath(instance_.android_efi_loader())
            .Architecture(instance_.target_arch());
        return android_efi_loader.Build();
      }
      case CuttlefishConfig::InstanceSpecific::BootFlow::ChromeOs: {
        auto linux = LinuxEspBuilder(instance_.esp_image_path());
        InitChromeOsArgs(linux);

        linux.Root("/dev/vda3")
            .Architecture(instance_.target_arch())
            .Kernel(instance_.chromeos_kernel_path());

        return linux.Build();
      }
      case CuttlefishConfig::InstanceSpecific::BootFlow::Linux: {
        auto linux = LinuxEspBuilder(instance_.esp_image_path());
        InitLinuxArgs(linux);

        linux.Root("/dev/vda2")
             .Architecture(instance_.target_arch())
             .Kernel(instance_.linux_kernel_path());

        if (!instance_.linux_initramfs_path().empty()) {
          linux.Initrd(instance_.linux_initramfs_path());
        }

        return linux.Build();
      }
      case CuttlefishConfig::InstanceSpecific::BootFlow::Fuchsia: {
        auto fuchsia = FuchsiaEspBuilder(instance_.esp_image_path());
        return fuchsia.Architecture(instance_.target_arch())
                      .Zedboot(instance_.fuchsia_zedboot_path())
                      .MultibootBinary(instance_.fuchsia_multiboot_bin_path())
                      .Build();
      }
      default:
        break;
    }

    return true;
  }

  void InitLinuxArgs(LinuxEspBuilder& linux) {
    linux.Root("/dev/vda2");

    linux.Argument("console", "hvc0")
         .Argument("panic", "-1")
         .Argument("noefi");

    switch (instance_.target_arch()) {
      case Arch::Arm:
      case Arch::Arm64:
        linux.Argument("console", "ttyAMA0");
        break;
      case Arch::RiscV64:
        linux.Argument("console", "ttyS0");
        break;
      case Arch::X86:
      case Arch::X86_64:
        linux.Argument("console", "ttyS0")
             .Argument("pnpacpi", "off")
             .Argument("acpi", "noirq")
             .Argument("reboot", "k")
             .Argument("noexec", "off");
        break;
    }
  }

  void InitChromeOsArgs(LinuxEspBuilder& linux) {
    linux.Root("/dev/vda2")
        .Argument("console", "ttyS0")
        .Argument("panic", "-1")
        .Argument("noefi")
        .Argument("init=/sbin/init")
        .Argument("boot=local")
        .Argument("rootwait")
        .Argument("noresume")
        .Argument("noswap")
        .Argument("loglevel=7")
        .Argument("noinitrd")
        .Argument("cros_efi")
        .Argument("cros_debug")
        .Argument("earlyprintk=serial,ttyS0,115200")
        .Argument("earlycon=uart8250,io,0x3f8")
        .Argument("pnpacpi", "off")
        .Argument("acpi", "noirq")
        .Argument("reboot", "k")
        .Argument("noexec", "off");
  }

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
