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

#include "cuttlefish/host/commands/assemble_cvd/boot_image/vendor_boot_image_builder.h"

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

static std::unique_ptr<ReaderSeeker> PagePadding(uint64_t size,
                                                 uint64_t page_size) {
  uint64_t padding = (page_size - (size % page_size)) % page_size;
  return InMemoryIo(std::vector<char>(padding));
}

VendorBootImageBuilder& VendorBootImageBuilder::PageSize(uint32_t page_size) & {
  page_size_ = page_size;
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::PageSize(uint32_t page_size) && {
  return std::move(this->PageSize(page_size));
}

VendorBootImageBuilder& VendorBootImageBuilder::KernelAddr(
    uint32_t kernel_addr) & {
  kernel_addr_ = kernel_addr;
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::KernelAddr(
    uint32_t kernel_addr) && {
  return std::move(this->KernelAddr(kernel_addr));
}

VendorBootImageBuilder& VendorBootImageBuilder::RamdiskAddr(
    uint32_t ramdisk_addr) & {
  ramdisk_addr_ = ramdisk_addr;
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::RamdiskAddr(
    uint32_t ramdisk_addr) && {
  return std::move(this->RamdiskAddr(ramdisk_addr));
}

VendorBootImageBuilder& VendorBootImageBuilder::VendorRamdisk(
    std::unique_ptr<ReaderSeeker> vendor_ramdisk) & {
  vendor_ramdisk_ = std::move(vendor_ramdisk);
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::VendorRamdisk(
    std::unique_ptr<ReaderSeeker> vendor_ramdisk) && {
  return std::move(this->VendorRamdisk(std::move(vendor_ramdisk)));
}

VendorBootImageBuilder& VendorBootImageBuilder::KernelCommandLine(
    std::string_view cmdline) & {
  kernel_command_line_ = cmdline;
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::KernelCommandLine(
    std::string_view cmdline) && {
  return std::move(this->KernelCommandLine(cmdline));
}

VendorBootImageBuilder& VendorBootImageBuilder::TagsAddr(uint32_t tags) & {
  tags_addr_ = tags;
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::TagsAddr(uint32_t tags) && {
  return std::move(this->TagsAddr(tags));
}

VendorBootImageBuilder& VendorBootImageBuilder::Name(std::string_view name) & {
  name_ = name;
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::Name(std::string_view name) && {
  return std::move(this->Name(name));
}

VendorBootImageBuilder& VendorBootImageBuilder::Dtb(
    std::unique_ptr<ReaderSeeker> dtb) & {
  dtb_ = std::move(dtb);
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::Dtb(
    std::unique_ptr<ReaderSeeker> dtb) && {
  return std::move(this->Dtb(std::move(dtb)));
}

VendorBootImageBuilder& VendorBootImageBuilder::DtbAddr(uint64_t dtb) & {
  dtb_addr_ = dtb;
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::DtbAddr(uint64_t dtb) && {
  return std::move(this->DtbAddr(dtb));
}

VendorBootImageBuilder& VendorBootImageBuilder::Bootconfig(
    std::unique_ptr<ReaderSeeker> bootconfig) & {
  bootconfig_ = std::move(bootconfig);
  return *this;
}
VendorBootImageBuilder VendorBootImageBuilder::Bootconfig(
    std::unique_ptr<ReaderSeeker> bootconfig) && {
  return std::move(this->Bootconfig(std::move(bootconfig)));
}

Result<ConcatReaderSeeker> VendorBootImageBuilder::BuildV4() {
  vendor_boot_img_hdr_v4 header;
  memcpy(header.magic, VENDOR_BOOT_MAGIC, sizeof(header.magic));
  header.header_version = 4;
  header.page_size = page_size_;
  header.kernel_addr = kernel_addr_;
  header.ramdisk_addr = ramdisk_addr_;
  header.vendor_ramdisk_size =
      vendor_ramdisk_ ? CF_EXPECT(Length(*vendor_ramdisk_)) : 0;
  CF_EXPECT_LT(kernel_command_line_.size(), sizeof(header.cmdline));
  snprintf(reinterpret_cast<char*>(header.cmdline), sizeof(header.cmdline),
           "%s", kernel_command_line_.c_str());
  header.tags_addr = tags_addr_;
  CF_EXPECT_LT(name_.size(), sizeof(header.name));
  snprintf(reinterpret_cast<char*>(header.name), sizeof(header.name), "%s",
           name_.c_str());
  header.header_size = sizeof(header);
  header.dtb_size = dtb_ ? CF_EXPECT(Length(*dtb_)) : 0;
  header.dtb_addr = dtb_addr_;
  header.vendor_ramdisk_table_size = 0;
  header.vendor_ramdisk_table_entry_num = 0;
  header.vendor_ramdisk_table_entry_size =
      sizeof(vendor_ramdisk_table_entry_v4);
  header.bootconfig_size = bootconfig_ ? CF_EXPECT(Length(*bootconfig_)) : 0;

  std::vector<std::unique_ptr<ReaderSeeker>> members;
  members.emplace_back(InMemoryIo(
      std::string_view(reinterpret_cast<char*>(&header), sizeof(header))));
  members.emplace_back(PagePadding(sizeof(header), page_size_));
  if (vendor_ramdisk_) {
    members.emplace_back(std::move(vendor_ramdisk_));
    members.emplace_back(PagePadding(header.vendor_ramdisk_size, page_size_));
  }
  if (dtb_) {
    members.emplace_back(std::move(dtb_));
    members.emplace_back(PagePadding(header.dtb_size, page_size_));
  }
  // No vendor ramdisk table
  if (bootconfig_) {
    members.emplace_back(std::move(bootconfig_));
    members.emplace_back(PagePadding(header.bootconfig_size, page_size_));
  }
  return CF_EXPECT(ConcatReaderSeeker::Create(std::move(members)));
}

}  // namespace cuttlefish
