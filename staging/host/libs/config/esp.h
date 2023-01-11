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

#pragma once

#include <utility>
#include <optional>
#include <string>
#include <vector>

#include "host/libs/config/cuttlefish_config.h"
#include "common/libs/utils/environment.h"

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
static constexpr char kBootSrcPathIA32[] = "/usr/lib/grub/i386-efi/monolithic/grubia32.efi";
static constexpr char kBootDestPathIA32[] = "/EFI/BOOT/BOOTIA32.EFI";

static constexpr char kBootSrcPathAA64[] = "/usr/lib/grub/arm64-efi/monolithic/grubaa64.efi";
static constexpr char kBootDestPathAA64[] = "/EFI/BOOT/BOOTAA64.EFI";

static constexpr char kMultibootModuleSrcPathIA32[] = "/usr/lib/grub/i386-efi/multiboot.mod";
static constexpr char kMultibootModuleDestPathIA32[] = "/EFI/modules/multiboot.mod";

static constexpr char kMultibootModuleSrcPathAA64[] = "/usr/lib/grub/arm64-efi/multiboot.mod";
static constexpr char kMultibootModuleDestPathAA64[] = "/EFI/modules/multiboot.mod";

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

const std::vector<std::string> kGrubModulesX86 =
    {"normal", "configfile", "linux", "linuxefi", "multiboot",
     "ls", "cat", "help", "fat", "part_msdos", "part_gpt"};
static constexpr char kGrubModulesPath[] = "/usr/lib/grub/";
static constexpr char kGrubModulesX86Name[] = "i386-efi";
static constexpr char kGrubModulesArm64Name[] = "arm64-efi";

class LinuxEspBuilder final {
 public:
  LinuxEspBuilder() = delete;
  LinuxEspBuilder(std::string image_path): image_path_(std::move(image_path)) {}

  LinuxEspBuilder& Argument(std::string key, std::string value) &;
  LinuxEspBuilder& Argument(std::string value) &;
  LinuxEspBuilder& Root(std::string root) &;
  LinuxEspBuilder& Kernel(std::string kernel) &;
  LinuxEspBuilder& Initrd(std::string initrd) &;
  LinuxEspBuilder& Architecture(Arch arch) &;

  bool Build() const;

 private:
  std::string DumpConfig() const;

  const std::string image_path_;
  std::vector<std::pair<std::string, std::string>> arguments_;
  std::vector<std::string> single_arguments_;
  std::string root_;
  std::string kernel_;
  std::string initrd_;
  std::optional<Arch> arch_;
};

class FuchsiaEspBuilder {
 public:
  FuchsiaEspBuilder() = delete;
  FuchsiaEspBuilder(std::string image_path): image_path_(std::move(image_path)) {}

  FuchsiaEspBuilder& MultibootBinary(std::string multiboot) &;
  FuchsiaEspBuilder& Zedboot(std::string zedboot) &;
  FuchsiaEspBuilder& Architecture(Arch arch) &;

  bool Build() const;

 private:
  std::string DumpConfig() const;

  const std::string image_path_;
  std::string multiboot_bin_;
  std::string zedboot_;
  std::optional<Arch> arch_;
};

bool NewfsMsdos(const std::string& data_image, int data_image_mb,
                int offset_num_mb);

bool CanGenerateEsp(Arch arch);

} // namespace cuttlefish
