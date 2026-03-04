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

#include <stdio.h>

#include <memory>
#include <string>
#include <utility>

#include "bootimg.h"

#include "cuttlefish/io/concat.h"
#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/length.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

static std::unique_ptr<ReaderSeeker> PagePadding(uint64_t size) {
  static constexpr uint64_t kPageSize = 4096;
  uint64_t padding = (kPageSize - (size % kPageSize)) % kPageSize;
  return InMemoryIo(std::vector<char>(padding));
}

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

Result<ConcatReaderSeeker> BootImageBuilder::BuildV4() {
  boot_img_hdr_v4 header;
  memcpy(header.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE);
  header.kernel_size = kernel_ ? CF_EXPECT(Length(*kernel_)) : 0;
  header.ramdisk_size = ramdisk_ ? CF_EXPECT(Length(*ramdisk_)) : 0;
  header.os_version = os_version_;
  header.header_size = sizeof(header);
  header.header_version = 4;
  CF_EXPECT_LT(kernel_command_line_.size(), sizeof(header.cmdline));
  snprintf(reinterpret_cast<char*>(header.cmdline), sizeof(header.cmdline),
           "%s", kernel_command_line_.c_str());
  header.signature_size = signature_ ? CF_EXPECT(Length(*signature_)) : 0;

  std::vector<std::unique_ptr<ReaderSeeker>> members;
  members.emplace_back(InMemoryIo(
      std::string_view(reinterpret_cast<char*>(&header), sizeof(header))));
  members.emplace_back(PagePadding(sizeof(header)));
  if (kernel_) {
    members.emplace_back(std::move(kernel_));
    members.emplace_back(PagePadding(header.kernel_size));
  }
  if (ramdisk_) {
    members.emplace_back(std::move(ramdisk_));
    members.emplace_back(PagePadding(header.ramdisk_size));
  }
  if (signature_) {
    members.emplace_back(std::move(signature_));
    members.emplace_back(PagePadding(header.signature_size));
  }

  return CF_EXPECT(ConcatReaderSeeker::Create(std::move(members)));
}

}  // namespace cuttlefish
