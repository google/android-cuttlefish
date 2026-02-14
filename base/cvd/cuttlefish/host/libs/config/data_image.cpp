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
#include "cuttlefish/host/libs/config/data_image.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/libs/config/ap_boot_flow.h"
#include "cuttlefish/host/libs/config/boot_flow.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image_policy.h"
#include "cuttlefish/host/libs/config/esp.h"
#include "cuttlefish/host/libs/config/openwrt_args.h"
#include "cuttlefish/host/libs/image_aggregator/mbr.h"
#include "cuttlefish/result/result.h"

// https://cs.android.com/android/platform/superproject/main/+/main:device/google/cuttlefish/Android.bp;l=127;drc=6f7d6a4db58efcc2ddd09eda07e009c6329414cd
#define F2FS_BLOCKSIZE "4096"

namespace cuttlefish {

namespace {

const int FSCK_ERROR_CORRECTED = 1;
const int FSCK_ERROR_CORRECTED_REQUIRES_REBOOT = 2;

Result<void> ForceFsckImage(
    const std::string& data_image,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::string fsck_path;
  if (instance.userdata_format() == "f2fs") {
    fsck_path = HostBinaryPath("fsck.f2fs");
  } else if (instance.userdata_format() == "ext4") {
    fsck_path = HostBinaryPath("e2fsck");
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
  CF_EXPECTF(data_image_mb >= file_mb, "'{}' is already {} MB, won't downsize",
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
    resize_path = HostBinaryPath("resize2fs");
  }
  if (!resize_path.empty()) {
    CF_EXPECT_EQ(Execute({resize_path, data_image}), 0,
                 "`" << resize_path << " " << data_image << "` failed");
    CF_EXPECT(ForceFsckImage(data_image, instance));
  }

  return {};
}

std::string GetFsType(const std::string& path) {
  Command command("/usr/sbin/blkid");
  command.AddParameter(path);

  Result<std::string> blkid_out = RunAndCaptureStdout(std::move(command));
  if (!blkid_out.ok()) {
    LOG(ERROR) << "`blkid '" << path << "'` failed: " << blkid_out.error();
    return "";
  }

  static constexpr std::string_view kTypePrefix = "TYPE=\"";

  size_t type_begin = blkid_out->find(kTypePrefix);
  if (type_begin == std::string::npos) {
    LOG(ERROR) << "blkid did not report a TYPE. stdout='" << *blkid_out << "'";
    return "";
  }
  type_begin += kTypePrefix.size();

  size_t type_end = blkid_out->find('"', type_begin);
  if (type_end == std::string::npos) {
    LOG(ERROR) << "unable to find the end of the blkid TYPE. stdout='"
               << *blkid_out << "'";
    return "";
  }

  return blkid_out->substr(type_begin, type_end - type_begin);
}

enum class DataImageAction { kNoAction, kResizeImage, kCreateBlankImage };

static Result<DataImageAction> ChooseDataImageAction(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (instance.data_policy() == DataImagePolicy::AlwaysCreate) {
    return DataImageAction::kCreateBlankImage;
  }
  if (!FileHasContent(instance.data_image())) {
    return DataImageAction::kCreateBlankImage;
  }
  if (instance.data_policy() == DataImagePolicy::UseExisting) {
    return DataImageAction::kNoAction;
  }
  auto current_fs_type = GetFsType(instance.data_image());
  if (current_fs_type != instance.userdata_format()) {
    CF_EXPECT(instance.data_policy() != DataImagePolicy::ResizeUpTo,
              "Changing the fs format is incompatible with --data_policy="
                  << DataImagePolicyString(DataImagePolicy::ResizeUpTo)
                  << " (\"" << current_fs_type << "\" != \""
                  << instance.userdata_format() << "\")");
    return DataImageAction::kCreateBlankImage;
  }
  if (instance.data_policy() == DataImagePolicy::ResizeUpTo) {
    return DataImageAction::kResizeImage;
  }
  return DataImageAction::kNoAction;
}

} // namespace

Result<void> CreateBlankImage(const std::string& image, int num_mb,
                              const std::string& image_fmt) {
  VLOG(0) << "Creating " << image;

  off_t image_size_bytes = static_cast<off_t>(num_mb) << 20;
  // MakeFatImage will do the same as below to zero the image files, so we
  // don't need to do it here
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
    CF_EXPECT(MakeFatImage(image, num_mb, 1), "Failed to create SD-Card fs");
    // Write the MBR after the filesystem is formatted, as the formatting tools
    // don't consistently preserve the image contents
    MasterBootRecord mbr = {
        .partitions = {{
            .partition_type = 0xC,
            .first_lba = (uint32_t)offset_size_bytes / kSectorSize,
            .num_sectors = (uint32_t)image_size_bytes / kSectorSize,
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
      VLOG(0) << instance.data_image() << " exists. Not creating it.";
      return {};
    case DataImageAction::kCreateBlankImage: {
      if (Result<void> res = RemoveFile(instance.new_data_image()); !res.ok()) {
        LOG(ERROR) << res.error();
      }
      CF_EXPECT(instance.blank_data_image_mb() != 0,
                "Expected `-blank_data_image_mb` to be set for "
                "image creation.");
      CF_EXPECT(CreateBlankImage(instance.new_data_image(),
                                 instance.blank_data_image_mb(), "none"),
                "Failed to create a blank image at \""
                    << instance.new_data_image() << "\" with size "
                    << instance.blank_data_image_mb() << "\"");
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

static bool EspRequiredForBootFlow(BootFlow flow) {
  return flow == BootFlow::AndroidEfiLoader || flow == BootFlow::ChromeOs ||
         flow == BootFlow::Linux || flow == BootFlow::Fuchsia;
}

static bool EspRequiredForAPBootFlow(APBootFlow ap_boot_flow) {
  return ap_boot_flow == APBootFlow::Grub;
}

static void InitLinuxArgs(Arch target_arch, LinuxEspBuilder& linux_esp_builder) {
  linux_esp_builder.Root("/dev/vda2");

  linux_esp_builder.Argument("console", "hvc0").Argument("panic", "-1").Argument("noefi");

  switch (target_arch) {
    case Arch::Arm:
    case Arch::Arm64:
      linux_esp_builder.Argument("console", "ttyAMA0");
      break;
    case Arch::RiscV64:
      linux_esp_builder.Argument("console", "ttyS0");
      break;
    case Arch::X86:
    case Arch::X86_64:
      linux_esp_builder.Argument("console", "ttyS0")
          .Argument("pnpacpi", "off")
          .Argument("acpi", "noirq")
          .Argument("reboot", "k")
          .Argument("noexec", "off");
      break;
  }
}

static void InitChromeOsArgs(LinuxEspBuilder& linux_esp_builder) {
  linux_esp_builder.Root("/dev/vda2")
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

static bool BuildAPImage(const CuttlefishConfig& config,
                         const CuttlefishConfig::InstanceSpecific& instance) {
  auto linux_esp_builder = LinuxEspBuilder(instance.ap_esp_image_path());
  InitLinuxArgs(instance.target_arch(), linux_esp_builder);

  auto openwrt_args = OpenwrtArgsFromConfig(instance);
  for (auto& openwrt_arg : openwrt_args) {
    linux_esp_builder.Argument(openwrt_arg.first, openwrt_arg.second);
  }

  linux_esp_builder.Root("/dev/vda2")
      .Architecture(instance.target_arch())
      .Kernel(config.ap_kernel_image());

  return linux_esp_builder.Build();
}

static bool BuildOSImage(const CuttlefishConfig::InstanceSpecific& instance) {
  switch (instance.boot_flow()) {
    case BootFlow::AndroidEfiLoader: {
      auto android_efi_loader =
          AndroidEfiLoaderEspBuilder(instance.esp_image_path());
      android_efi_loader.EfiLoaderPath(instance.android_efi_loader())
          .Architecture(instance.target_arch());
      return android_efi_loader.Build();
    }
    case BootFlow::ChromeOs: {
      auto linux_esp_builder = LinuxEspBuilder(instance.esp_image_path());
      InitChromeOsArgs(linux_esp_builder);

      linux_esp_builder.Root("/dev/vda3")
          .Architecture(instance.target_arch())
          .Kernel(instance.chromeos_kernel_path());

      return linux_esp_builder.Build();
    }
    case BootFlow::Linux: {
      auto linux_esp_builder = LinuxEspBuilder(instance.esp_image_path());
      InitLinuxArgs(instance.target_arch(), linux_esp_builder);

      linux_esp_builder.Root("/dev/vda2")
          .Architecture(instance.target_arch())
          .Kernel(instance.linux_kernel_path());

      if (!instance.linux_initramfs_path().empty()) {
        linux_esp_builder.Initrd(instance.linux_initramfs_path());
      }

      return linux_esp_builder.Build();
    }
    case BootFlow::Fuchsia: {
      auto fuchsia = FuchsiaEspBuilder(instance.esp_image_path());
      return fuchsia.Architecture(instance.target_arch())
          .Zedboot(instance.fuchsia_zedboot_path())
          .MultibootBinary(instance.fuchsia_multiboot_bin_path())
          .Build();
    }
    default:
      break;
  }

  return true;
}

Result<void> InitializeEspImage(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (EspRequiredForAPBootFlow(instance.ap_boot_flow())) {
    VLOG(0) << "creating esp_image: " << instance.ap_esp_image_path();
    CF_EXPECT(BuildAPImage(config, instance));
  }
  if (EspRequiredForBootFlow(instance.boot_flow()) &&
      !VmManagerIsGem5(config)) {
    VLOG(0) << "creating esp_image: " << instance.esp_image_path();
    CF_EXPECT(BuildOSImage(instance));
  }
  return {};
}

} // namespace cuttlefish
