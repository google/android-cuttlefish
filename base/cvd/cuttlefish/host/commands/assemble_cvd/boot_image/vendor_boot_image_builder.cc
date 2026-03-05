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
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/length.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

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
  return CF_ERR("unimplemented");
}

}  // namespace cuttlefish
