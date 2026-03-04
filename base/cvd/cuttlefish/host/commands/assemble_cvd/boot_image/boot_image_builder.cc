//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/boot_image/boot_image_builder.h"

#include <memory>
#include <string>
#include <utility>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

BootImageBuilder& BootImageBuilder::KernelCommandLine(std::string_view cmd) & {
  kernel_command_line_ = cmd;
  return *this;
}
BootImageBuilder BootImageBuilder::KernelCommandLine(std::string_view cmd) && {
  return std::move(this->KernelCommandLine(cmd));
}

BootImageBuilder& BootImageBuilder::Kernel(std::unique_ptr<ReaderSeeker> rd) & {
  kernel_ = std::move(rd);
  return *this;
}
BootImageBuilder BootImageBuilder::Kernel(std::unique_ptr<ReaderSeeker> rd) && {
  return std::move(this->Kernel(std::move(rd)));
}

BootImageBuilder& BootImageBuilder::Ramdisk(std::unique_ptr<ReaderSeeker> r) & {
  ramdisk_ = std::move(r);
  return *this;
}
BootImageBuilder BootImageBuilder::Ramdisk(std::unique_ptr<ReaderSeeker> r) && {
  return std::move(this->Ramdisk(std::move(r)));
}

BootImageBuilder& BootImageBuilder::Signature(
    std::unique_ptr<ReaderSeeker> signature) & {
  signature_ = std::move(signature);
  return *this;
}
BootImageBuilder BootImageBuilder::Signature(
    std::unique_ptr<ReaderSeeker> signature) && {
  return std::move(this->Signature(std::move(signature)));
}

BootImageBuilder& BootImageBuilder::OsVersion(uint32_t os_version) & {
  os_version_ = os_version;
  return *this;
}
BootImageBuilder BootImageBuilder::OsVersion(uint32_t os_version) && {
  return std::move(this->OsVersion(os_version));
}

Result<std::unique_ptr<ReaderSeeker>> BootImageBuilder::BuildV4() {
  return CF_ERR("TODO");
}

}  // namespace cuttlefish
