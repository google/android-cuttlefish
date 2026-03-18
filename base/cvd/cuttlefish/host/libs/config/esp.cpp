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

#include "cuttlefish/host/libs/config/esp.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/config/esp/esp_builder.h"

namespace cuttlefish {

// For licensing and build reproducibility reasons, pick up the bootloaders
// from the host Linux distribution (if present) and pack them into the
// automatically generated ESP. If the user wants their own bootloaders,
// they can use -esp_image=/path/to/esp.img to override, so we don't need
// to accommodate customizations of this packing process.
//
// Currently we only support Debian based distributions, and GRUB is built
// for those distros to always load grub.cfg from EFI/debian/grub.cfg, and
// nowhere else. If you want to add support for other distros, make the
// extra directories below and copy the initial grub.cfg there as well
//
// NOTE: For now, just ignore the 32-bit ARM version, as Debian doesn't
//       build an EFI monolith for this architecture.
static constexpr char kBootSrcPathX64[] =
    "/usr/lib/grub/x86_64-efi/monolithic/grubx64.efi";
static constexpr char kBootDestPathX64[] = "/EFI/BOOT/BOOTX64.EFI";

static constexpr char kBootSrcPathAA64[] =
    "/usr/lib/grub/arm64-efi/monolithic/grubaa64.efi";
static constexpr char kBootDestPathAA64[] = "/EFI/BOOT/BOOTAA64.EFI";

static constexpr char kBootDestPathRiscV64[] = "/EFI/BOOT/BOOTRISCV64.EFI";

static constexpr char kMultibootModuleSrcPathX64[] =
    "/usr/lib/grub/x86_64-efi/multiboot.mod";
static constexpr char kMultibootModuleSrcPathAA64[] =
    "/usr/lib/grub/arm64-efi/multiboot.mod";
static constexpr char kMultibootModuleDestPath[] = "/EFI/modules/multiboot.mod";

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

static constexpr std::array kGrubModules{
    "normal", "configfile", "linux", "multiboot",  "ls",
    "cat",    "help",       "fat",   "part_msdos", "part_gpt"};
static constexpr char kGrubModulesPath[] = "/usr/lib/grub";
static constexpr char kGrubModulesX64Name[] = "x86_64-efi";

bool CanGenerateGrubEsp(Arch arch) {
  switch (arch) {
    case Arch::RiscV64:
      return false;
    case Arch::Arm:
    case Arch::Arm64: {
      return FileExists(kBootSrcPathAA64);
    }
    case Arch::X86:
    case Arch::X86_64: {
      for (std::string_view module_name : kGrubModules) {
        const std::string path =
            absl::StrCat(kGrubModulesPath, "/", kGrubModulesX64Name, "/",
                         module_name, ".mod");
        if (!FileExists(path)) {
          return FileExists(kBootSrcPathX64);
        }
      }
      return true;
    }
  }
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

EspBuilder PrepareGrubESP(const std::string& image_path, Arch arch) {
  auto builder = EspBuilder(image_path);
  builder.Directory("EFI").Directory("EFI/BOOT").Directory("EFI/modules");

  switch (arch) {
    case Arch::Arm:
    case Arch::Arm64:
      builder.File(kBootSrcPathAA64, kBootDestPathAA64, /* required */ true);
      // Not required for arm64 due missing it in deb package, so fuchsia is
      // not supported for it.
      builder.File(kMultibootModuleSrcPathAA64, kMultibootModuleDestPath,
                   /* required */ false);
      break;
    case Arch::RiscV64:
      // FIXME: Implement
      break;
    case Arch::X86:
    case Arch::X86_64: {
      const auto efi_path = image_path + ".efi";
      const auto x64_modules =
          absl::StrCat(kGrubModulesPath, "/", kGrubModulesX64Name);

      if (GrubMakeImage(kGrubConfigDestDirectoryPath, kGrubModulesX64Name,
                        x64_modules, efi_path, kGrubModules)) {
        LOG(INFO) << "Loading grub_mkimage generated EFI binary for X86_64";
        builder.File(efi_path, kBootDestPathX64, /* required */ true);
      } else {
        LOG(INFO) << "Loading prebuilt monolith EFI binary for X86_64";
        builder.File(kBootSrcPathX64, kBootDestPathX64, /* required */ true);
        builder.File(kMultibootModuleSrcPathX64, kMultibootModuleDestPath,
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
    case Arch::X86_64:
      dest_path = kBootDestPathX64;
      break;
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

  auto builder = PrepareGrubESP(image_path_, *arch_);

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

  auto builder = PrepareGrubESP(image_path_, *arch_);

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
    << "  insmod " << kMultibootModuleDestPath << std::endl
    << "  multiboot " << kMultibootBinDestPath << std::endl
    << "  module " << kZedbootDestPath << std::endl
    << "}" << std::endl;

  return o.str();
}

} // namespace cuttlefish
