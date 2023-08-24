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

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/libs/utils/environment.h"

namespace cuttlefish {

class AndroidEfiLoaderEspBuilder final {
 public:
  AndroidEfiLoaderEspBuilder() = delete;
  AndroidEfiLoaderEspBuilder(std::string image_path)
      : image_path_(std::move(image_path)) {}

  AndroidEfiLoaderEspBuilder& EfiLoaderPath(std::string efi_loader_path) &;
  AndroidEfiLoaderEspBuilder& Architecture(Arch arch) &;

  bool Build() const;

 private:
  const std::string image_path_;
  std::string efi_loader_path_;
  Arch arch_;
};

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
