/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "host/commands/assemble_cvd/boot_image_unpacker.h"

#include <string.h>
#include <unistd.h>

#include <sstream>

#include <bootimg.h>
#include <android-base/logging.h>

#include "common/libs/utils/subprocess.h"

namespace cvd {

namespace {

// Extracts size bytes from file, starting at offset bytes from the beginning to
// path.
bool ExtractFile(SharedFD source, off_t offset, size_t size,
                 const std::string& path) {
  auto dest = SharedFD::Open(path.c_str(), O_CREAT | O_RDWR, 0755);
  if (!dest->IsOpen()) {
    LOG(ERROR) << "Unable to open " << path;
    return false;
  }
  auto off = source->LSeek(offset, SEEK_SET);
  if (off != offset) {
    LOG(ERROR) << "Failed to lseek: " << source->StrError();
    return false;
  }
  return dest->CopyFrom(*source, size);
}
}  // namespace

std::unique_ptr<BootImageUnpacker> BootImageUnpacker::FromImages(
    const std::string& boot_image_path,
    const std::string& vendor_boot_image_path) {
  auto boot_img = SharedFD::Open(boot_image_path.c_str(), O_RDONLY);
  if (!boot_img->IsOpen()) {
    LOG(ERROR) << "Unable to open boot image (" << boot_image_path
               << "): " << boot_img->StrError();
    return nullptr;
  }
  boot_img_hdr_v3 header;
  auto bytes_read = boot_img->Read(&header, sizeof(header));
  if (bytes_read != sizeof(header)) {
    LOG(ERROR) << "Error reading boot image header";
    return nullptr;
  }

  auto vendor_boot_img = SharedFD::Open(vendor_boot_image_path.c_str(),
                                        O_RDONLY);
  if (!vendor_boot_img->IsOpen()) {
    LOG(ERROR) << "Unable to open vendor boot image (" << vendor_boot_image_path
               << "): " << vendor_boot_img->StrError();
    return nullptr;
  }
  vendor_boot_img_hdr_v3 vboot_header;
  bytes_read = vendor_boot_img->Read(&vboot_header, sizeof(vboot_header));
  if (bytes_read != sizeof(vboot_header)) {
    LOG(ERROR) << "Error reading vendor boot image header";
    return nullptr;
  }

  std::ostringstream cmdline;
  cmdline << reinterpret_cast<char*>(&header.cmdline[0]);
  if (vboot_header.cmdline[0] != '\0') {
    cmdline << " ";
    cmdline << reinterpret_cast<char*>(&vboot_header.cmdline[0]);
  }

  uint32_t page_size = 4096;
  // See system/tools/mkbootimg/include/bootimg/bootimg.h for the origin of
  // these offset calculations
  uint32_t kernel_offset = page_size;
  uint32_t ramdisk_offset =
      kernel_offset +
      ((header.kernel_size + page_size - 1) / page_size) * page_size;
  uint32_t vendor_ramdisk_offset =
      ((vboot_header.header_size + vboot_header.page_size - 1) / vboot_header.page_size) *
      vboot_header.page_size;

  std::unique_ptr<BootImageUnpacker> ret(new BootImageUnpacker(
      boot_img, cmdline.str(), header.kernel_size, kernel_offset,
      header.ramdisk_size, ramdisk_offset, vendor_boot_img,
      vboot_header.vendor_ramdisk_size, vendor_ramdisk_offset));

  return ret;
}

std::string BootImageUnpacker::kernel_cmdline() const {
  return kernel_cmdline_;
}

bool BootImageUnpacker::ExtractKernelImage(const std::string& path) const {
  if (kernel_image_size_ == 0) return false;
  return ExtractFile(boot_image_, kernel_image_offset_, kernel_image_size_,
                     path);
}
bool BootImageUnpacker::ExtractRamdiskImage(const std::string& path) const {
  if (ramdisk_image_size_ == 0) return false;
  return ExtractFile(boot_image_, ramdisk_image_offset_, ramdisk_image_size_,
                     path);
}
bool BootImageUnpacker::ExtractVendorRamdiskImage(const std::string& path) const {
  if (vendor_ramdisk_image_size_ == 0) return false;
  return ExtractFile(vendor_boot_image_, vendor_ramdisk_image_offset_,
                     vendor_ramdisk_image_size_, path);
}

bool BootImageUnpacker::Unpack(const std::string& ramdisk_image_path,
                               const std::string& vendor_ramdisk_image_path,
                               const std::string& kernel_image_path) {
  if (HasRamdiskImage()) {
    if (!ExtractRamdiskImage(ramdisk_image_path)) {
      LOG(ERROR) << "Error extracting ramdisk from boot image";
      return false;
    }
  }
  if (HasVendorRamdiskImage()) {
    if (!ExtractVendorRamdiskImage(vendor_ramdisk_image_path)) {
      LOG(ERROR) << "Error extracting vendor ramdisk from venodr boot image";
      return false;
    }
  }
  if (!kernel_image_path.empty()) {
    if (HasKernelImage()) {
      if (!ExtractKernelImage(kernel_image_path)) {
        LOG(ERROR) << "Error extracting kernel from boot image";
        return false;
      }
    } else {
      LOG(ERROR) << "No kernel found on boot image";
      return false;
    }
  }
  return true;
}

}  // namespace cvd
