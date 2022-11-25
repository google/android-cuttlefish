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

#include <string>
#include <vector>

namespace cuttlefish {

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

class EspBuilder final {
 public:
  EspBuilder() = delete;
  EspBuilder(std::string image_path): image_path_(std::move(image_path)) {}

  EspBuilder& File(std::string from, std::string to, bool required) &;
  EspBuilder File(std::string from, std::string to, bool required) &&;

  EspBuilder& File(std::string from, std::string to) &;
  EspBuilder File(std::string from, std::string to) &&;

  EspBuilder& Directory(std::string path) &;
  EspBuilder Directory(std::string path) &&;

  bool Build() const;

 private:
  std::string image_path_;

  struct FileToAdd {
    std::string from;
    std::string to;
    bool required;
  };
  std::vector<std::string> directories_;
  std::vector<FileToAdd> files_;
};

bool NewfsMsdos(const std::string& data_image, int data_image_mb,
                int offset_num_mb);

} // namespace cuttlefish
