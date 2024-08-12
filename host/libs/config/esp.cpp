//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "host/libs/config/esp.h"
#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

// For licensing and build reproducibility reasons, pick up the bootloaders
// from the host Linux distribution (if present) and pack them into the
// automatically generated ESP. If the user wants their own bootloaders,
// they can use -esp_image=/path/to/esp.img to override, so we don't need
// to accommodate customizations of this packing process.

// Currently we only support Debian based distributions, and GRUB is built
// for those distros to always load grub.cfg from EFI/debian/grub.cfg, and
// nowhere else. If you want to add support for other distros, make the
// extra directories below and copy the initial grub.cfg there as well
//
// Currently the Cuttlefish bootloaders are built only for x86 (32-bit),
// ARM (QEMU only, 32-bit) and AArch64 (64-bit), and U-Boot will hard-code
// these search paths. Install all bootloaders to one of these paths.
// NOTE: For now, just ignore the 32-bit ARM version, as Debian doesn't
//       build an EFI monolith for this architecture.
// These are the paths Debian installs the monoliths to. If another distro
// uses an alternative monolith path, add it to this table
static constexpr char kBootSrcPathIA32[] =
    "/usr/lib/grub/i386-efi/monolithic/grubia32.efi";
static constexpr char kBootDestPathIA32[] = "/EFI/BOOT/BOOTIA32.EFI";

static constexpr char kBootSrcPathAA64[] =
    "/usr/lib/grub/arm64-efi/monolithic/grubaa64.efi";
static constexpr char kBootDestPathAA64[] = "/EFI/BOOT/BOOTAA64.EFI";

static constexpr char kBootDestPathRiscV64[] = "/EFI/BOOT/BOOTRISCV64.EFI";

static constexpr char kMultibootModuleSrcPathIA32[] =
    "/usr/lib/grub/i386-efi/multiboot.mod";
static constexpr char kMultibootModuleDestPathIA32[] =
    "/EFI/modules/multiboot.mod";

static constexpr char kMultibootModuleSrcPathAA64[] =
    "/usr/lib/grub/arm64-efi/multiboot.mod";
static constexpr char kMultibootModuleDestPathAA64[] =
    "/EFI/modules/multiboot.mod";

static constexpr char kKernelDestPath[] = "/vmlinuz";
static constexpr char kInitrdDestPath[] = "/initrd";
static constexpr char kZedbootDestPath[] = "/zedboot.zbi";
static constexpr char kMultibootBinDestPath[] = "/multiboot.bin";

// TODO(b/260338443, b/260337906) remove ubuntu and debian variations
// after migrating to grub-mkimage or adding grub binaries as a prebuilt
static constexpr char kGrubDebianConfigDestPath[] = "/EFI/debian/grub.cfg";
static constexpr char kGrubUbuntuConfigDestPath[] = "/EFI/ubuntu/grub.cfg";
static constexpr char kGrubConfigDestDirectoryPath[] = "/boot/grub";
static constexpr char kGrubConfigDestPath[] = "/boot/grub/grub.cfg";

static constexpr std::array kGrubModulesX86{
    "normal", "configfile", "linux", "linuxefi",   "multiboot", "ls",
    "cat",    "help",       "fat",   "part_msdos", "part_gpt"};
static constexpr char kGrubModulesPath[] = "/usr/lib/grub/";
static constexpr char kGrubModulesX86Name[] = "i386-efi";

bool NewfsMsdos(const std::string& data_image, int data_image_mb,
                int offset_num_mb) {
  off_t image_size_bytes = static_cast<off_t>(data_image_mb) << 20;
  off_t offset_size_bytes = static_cast<off_t>(offset_num_mb) << 20;
  image_size_bytes -= offset_size_bytes;
  off_t image_size_sectors = image_size_bytes / 512;
  auto newfs_msdos_path = HostBinaryPath("newfs_msdos");
  return Execute({newfs_msdos_path,
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

bool CanGenerateEsp(Arch arch) {
  switch (arch) {
    case Arch::Arm:
    case Arch::Arm64:
    case Arch::RiscV64:
      // TODO(b/260960328) : Migrate openwrt image for arm64 into
      // APBootFlow::Grub.
      return false;
    case Arch::X86:
    case Arch::X86_64: {
      const auto x86_modules = std::string(kGrubModulesPath) + std::string(kGrubModulesX86Name);
      const auto modules_presented = std::all_of(
          kGrubModulesX86.begin(), kGrubModulesX86.end(),
          [&](const std::string& m) { return FileExists(x86_modules + m); });
      if (modules_presented) {
        return true;
      }

      const auto monolith_presented = FileExists(kBootSrcPathIA32);
      return monolith_presented;
    }
  }

  return false;
}

static bool MsdosMakeDirectories(const std::string& image_path,
                                 const std::vector<std::string>& directories) {
  auto mmd = HostBinaryPath("mmd");
  std::vector<std::string> command {mmd, "-i", image_path};
  command.insert(command.end(), directories.begin(), directories.end());

  const auto success = Execute(command);
  if (success != 0) {
    return false;
  }
  return true;
}

static bool CopyToMsdos(const std::string& image, const std::string& path,
                        const std::string& destination) {
  const auto mcopy = HostBinaryPath("mcopy");
  const auto success =
      Execute({mcopy, "-o", "-i", image, "-s", path, destination});
  if (success != 0) {
    return false;
  }
  return true;
}

template <typename T>
static bool GrubMakeImage(const std::string& prefix, const std::string& format,
                          const std::string& directory,
                          const std::string& output, const T& modules) {
  std::vector<std::string> command = {"grub-mkimage", "--prefix", prefix,
                                      "--format", format, "--directory", directory,
                                      "--output", output};
  std::move(modules.begin(), modules.end(), std::back_inserter(command));

  const auto success = Execute(command);
  return success == 0;
}

class EspBuilder final {
 public:
  EspBuilder() {}
  EspBuilder(std::string image_path): image_path_(std::move(image_path)) {}

  EspBuilder& File(std::string from, std::string to, bool required) & {
    files_.push_back(FileToAdd {std::move(from), std::move(to), required});
    return *this;
  }

  EspBuilder& File(std::string from, std::string to) & {
    return File(std::move(from), std::move(to), false);
  }

  EspBuilder& Directory(std::string path) & {
    directories_.push_back(std::move(path));
    return *this;
  }

  EspBuilder& Merge(EspBuilder builder) & {
    std::move(builder.directories_.begin(), builder.directories_.end(),
              std::back_inserter(directories_));
    std::move(builder.files_.begin(), builder.files_.end(),
              std::back_inserter(files_));
    return *this;
  }

  bool Build() {
    if (image_path_.empty()) {
      LOG(ERROR) << "Image path is required to build ESP. Empty constructor is intended to "
                 << "be used only for the merge functionality";
      return false;
    }

    // newfs_msdos won't make a partition smaller than 257 mb
    // this should be enough for anybody..
    const auto tmp_esp_image = image_path_ + ".tmp";
    if (!NewfsMsdos(tmp_esp_image, 257 /* mb */, 0 /* mb (offset) */)) {
      LOG(ERROR) << "Failed to create filesystem for " << tmp_esp_image;
      return false;
    }

    if (!MsdosMakeDirectories(tmp_esp_image, directories_)) {
      LOG(ERROR) << "Failed to create directories in " << tmp_esp_image;
      return false;
    }

    for (const FileToAdd& file : files_) {
      if (!FileExists(file.from)) {
        if (file.required) {
          LOG(ERROR) << "Failed to copy " << file.from << " to " << tmp_esp_image
                    << ": File does not exist";
          return false;
        }
        continue;
      }

      if (!CopyToMsdos(tmp_esp_image, file.from, "::" + file.to)) {
        LOG(ERROR) << "Failed to copy " << file.from << " to " << tmp_esp_image
                  << ": mcopy execution failed";
        return false;
      }
    }

    if (!RenameFile(tmp_esp_image, image_path_).ok()) {
      LOG(ERROR) << "Renaming " << tmp_esp_image << " to "
                  << image_path_ << " failed";
      return false;
    }

    return true;
  }

 private:
  const std::string image_path_;

  struct FileToAdd {
    std::string from;
    std::string to;
    bool required;
  };
  std::vector<std::string> directories_;
  std::vector<FileToAdd> files_;
};

EspBuilder PrepareESP(const std::string& image_path, Arch arch) {
  auto builder = EspBuilder(image_path);
  builder.Directory("EFI")
         .Directory("EFI/BOOT")
         .Directory("EFI/modules");

  const auto efi_path = image_path + ".efi";
  switch (arch) {
    case Arch::Arm:
    case Arch::Arm64:
      builder.File(kBootSrcPathAA64, kBootDestPathAA64, /* required */ true);
      // Not required for arm64 due missing it in deb package, so fuchsia is
      // not supported for it.
      builder.File(kMultibootModuleSrcPathAA64, kMultibootModuleDestPathAA64,
                    /* required */ false);
      break;
    case Arch::RiscV64:
      // FIXME: Implement
      break;
    case Arch::X86:
    case Arch::X86_64: {
      const auto x86_modules = std::string(kGrubModulesPath) + std::string(kGrubModulesX86Name);

      if (GrubMakeImage(kGrubConfigDestDirectoryPath, kGrubModulesX86Name,
                        x86_modules, efi_path, kGrubModulesX86)) {
        LOG(INFO) << "Loading grub_mkimage generated EFI binary";
        builder.File(efi_path, kBootDestPathIA32, /* required */ true);
      } else {
        LOG(INFO) << "Loading prebuilt monolith EFI binary";
        builder.File(kBootSrcPathIA32, kBootDestPathIA32, /* required */ true);
        builder.File(kMultibootModuleSrcPathIA32, kMultibootModuleDestPathIA32,
                     /* required */ true);
      }
      break;
    }
  }

  return builder;
}

// TODO(b/260338443, b/260337906) remove ubuntu and debian variations
// after migrating to grub-mkimage or adding grub binaries as a prebuilt
EspBuilder AddGrubConfig(const std::string& config) {
  auto builder = EspBuilder();

  builder.Directory("boot")
         .Directory("EFI/debian")
         .Directory("EFI/ubuntu")
         .Directory("boot/grub");

  builder.File(config, kGrubDebianConfigDestPath, /*required*/ true)
         .File(config, kGrubUbuntuConfigDestPath, /*required*/ true)
         .File(config, kGrubConfigDestPath, /*required*/ true);

  return builder;
}

AndroidEfiLoaderEspBuilder& AndroidEfiLoaderEspBuilder::EfiLoaderPath(
    std::string efi_loader_path) & {
  efi_loader_path_ = efi_loader_path;
  return *this;
}

AndroidEfiLoaderEspBuilder& AndroidEfiLoaderEspBuilder::Architecture(
    Arch arch) & {
  arch_ = arch;
  return *this;
}

bool AndroidEfiLoaderEspBuilder::Build() const {
  if (efi_loader_path_.empty()) {
    LOG(ERROR)
        << "Efi loader is required argument for AndroidEfiLoaderEspBuilder";
    return false;
  }
  EspBuilder builder = EspBuilder(image_path_);
  builder.Directory("EFI").Directory("EFI/BOOT");
  std::string dest_path;
  switch (arch_) {
    case Arch::Arm:
    case Arch::Arm64:
      dest_path = kBootDestPathAA64;
      break;
    case Arch::RiscV64:
      dest_path = kBootDestPathRiscV64;
      break;
    case Arch::X86:
    case Arch::X86_64: {
      dest_path = kBootDestPathIA32;
      break;
      default:
        LOG(ERROR) << "Unknown architecture";
        return false;
    }
  }
  builder.File(efi_loader_path_, dest_path, /* required */ true);
  return builder.Build();
}

LinuxEspBuilder& LinuxEspBuilder::Argument(std::string key, std::string value) & {
  arguments_.push_back({std::move(key), std::move(value)});
  return *this;
}

LinuxEspBuilder& LinuxEspBuilder::Argument(std::string value) & {
  single_arguments_.push_back(std::move(value));
  return *this;
}

LinuxEspBuilder& LinuxEspBuilder::Root(std::string root) & {
  root_ = std::move(root);
  return *this;
}

LinuxEspBuilder& LinuxEspBuilder::Kernel(std::string kernel) & {
  kernel_ = std::move(kernel);
  return *this;
}

LinuxEspBuilder& LinuxEspBuilder::Initrd(std::string initrd) & {
  initrd_ = std::move(initrd);
  return *this;
}

LinuxEspBuilder& LinuxEspBuilder::Architecture(Arch arch) & {
  arch_ = arch;
  return *this;
}

bool LinuxEspBuilder::Build() const {
  if (root_.empty()) {
    LOG(ERROR) << "Root is required argument for LinuxEspBuilder";
    return false;
  }
  if (kernel_.empty()) {
    LOG(ERROR) << "Kernel esp path is required argument for LinuxEspBuilder";
    return false;
  }
  if (!arch_) {
    LOG(ERROR) << "Architecture is required argument for LinuxEspBuilder";
    return false;
  }

  auto builder = PrepareESP(image_path_, *arch_);

  const auto tmp_grub_config = image_path_ + ".grub.cfg";
  const auto config_file = SharedFD::Creat(tmp_grub_config, 0644);
  if (!config_file->IsOpen()) {
    LOG(ERROR) << "Cannot create temporary grub config: " << tmp_grub_config;
    return false;
  }

  const auto dumped = DumpConfig();
  if (WriteAll(config_file, dumped) != dumped.size()) {
    LOG(ERROR) << "Failed to write grub config content to: " << tmp_grub_config;
    return false;
  }

  builder.Merge(AddGrubConfig(tmp_grub_config));
  builder.File(kernel_, kKernelDestPath, /*required*/ true);
  if (!initrd_.empty()) {
    builder.File(initrd_, kInitrdDestPath, /*required*/ true);
  }

  return builder.Build();
}

std::string LinuxEspBuilder::DumpConfig() const {
  std::ostringstream o;

  o << "set timeout=0" << std::endl
    << "menuentry \"Linux\" {" << std::endl
    << "  linux " << kKernelDestPath << " ";

  for (int i = 0; i < arguments_.size(); i++) {
    o << arguments_[i].first << "=" << arguments_[i].second << " ";
  }
  for (int i = 0; i < single_arguments_.size(); i++) {
    o << single_arguments_[i] << " ";
  }
  o << "root=" << root_ << std::endl;
  if (!initrd_.empty()) {
    o << "  if [ -e " << kInitrdDestPath << " ]; then" << std::endl;
    o << "    initrd " << kInitrdDestPath << std::endl;
    o << "  fi" << std::endl;
  }
  o << "}" << std::endl;

  return o.str();
}

FuchsiaEspBuilder& FuchsiaEspBuilder::MultibootBinary(std::string multiboot) & {
  multiboot_bin_ = std::move(multiboot);
  return *this;
}

FuchsiaEspBuilder& FuchsiaEspBuilder::Zedboot(std::string zedboot) & {
  zedboot_ = std::move(zedboot);
  return *this;
}

FuchsiaEspBuilder& FuchsiaEspBuilder::Architecture(Arch arch) & {
  arch_ = arch;
  return *this;
}

bool FuchsiaEspBuilder::Build() const {
  if (multiboot_bin_.empty()) {
    LOG(ERROR) << "Multiboot esp path is required argument for FuchsiaEspBuilder";
    return false;
  }
  if (zedboot_.empty()) {
    LOG(ERROR) << "Zedboot esp path is required argument for FuchsiaEspBuilder";
    return false;
  }
  if (!arch_) {
    LOG(ERROR) << "Architecture is required argument for FuchsiaEspBuilder";
    return false;
  }

  auto builder = PrepareESP(image_path_, *arch_);

  const auto tmp_grub_config = image_path_ + ".grub.cfg";
  const auto config_file = SharedFD::Creat(tmp_grub_config, 0644);
  if (!config_file->IsOpen()) {
    LOG(ERROR) << "Cannot create temporary grub config: " << tmp_grub_config;
    return false;
  }

  const auto dumped = DumpConfig();
  if (WriteAll(config_file, dumped) != dumped.size()) {
    LOG(ERROR) << "Failed to write grub config content to: " << tmp_grub_config;
    return false;
  }

  builder.Merge(AddGrubConfig(tmp_grub_config));
  builder.File(multiboot_bin_, kMultibootBinDestPath, /*required*/ true);
  builder.File(zedboot_, kZedbootDestPath, /*required*/ true);

  return builder.Build();
}

std::string FuchsiaEspBuilder::DumpConfig() const {
  std::ostringstream o;

  o << "set timeout=0" << std::endl
    << "menuentry \"Fuchsia\" {" << std::endl
    << "  insmod " << kMultibootModuleDestPathIA32 << std::endl
    << "  multiboot " << kMultibootBinDestPath << std::endl
    << "  module " << kZedbootDestPath << std::endl
    << "}" << std::endl;

  return o.str();
}

} // namespace cuttlefish
