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
#pragma once

#include <stdint.h>

#include <memory>
#include <string>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

// Unpacks the boot image and extracts kernel, ramdisk and kernel arguments
class BootImageUnpacker {
 public:
  // Reads header section of boot image at path and returns a BootImageUnpacker
  // preloaded with all the metadata.
  static std::unique_ptr<BootImageUnpacker> FromImages(
    const std::string& boot_image_path,
    const std::string& vendor_boot_image_path);

  ~BootImageUnpacker() = default;

  std::string kernel_cmdline() const;

  bool HasKernelImage() const { return kernel_image_size_ > 0; }
  bool HasRamdiskImage() const { return ramdisk_image_size_ > 0; }
  bool HasVendorRamdiskImage() const { return vendor_ramdisk_image_size_ > 0; }

  bool Unpack(const std::string& ramdisk_image_path,
              const std::string& vendor_ramdisk_image_path,
              const std::string& kernel_image_path);

 private:
  BootImageUnpacker(SharedFD boot_image, const std::string& cmdline,
                    uint32_t kernel_image_size, uint32_t kernel_image_offset,
                    uint32_t ramdisk_image_size, uint32_t ramdisk_image_offset,
                    SharedFD vendor_boot_image,
                    uint32_t vendor_ramdisk_image_size,
                    uint32_t vendor_ramdisk_image_offset)
      : boot_image_(boot_image),
        vendor_boot_image_(vendor_boot_image),
        kernel_cmdline_(cmdline),
        kernel_image_size_(kernel_image_size),
        kernel_image_offset_(kernel_image_offset),
        ramdisk_image_size_(ramdisk_image_size),
        ramdisk_image_offset_(ramdisk_image_offset),
        vendor_ramdisk_image_size_(vendor_ramdisk_image_size),
        vendor_ramdisk_image_offset_(vendor_ramdisk_image_offset) {}

  // Mutable because we only read from the fd, but do not modify its contents
  mutable SharedFD boot_image_;
  mutable SharedFD vendor_boot_image_;
  std::string kernel_cmdline_;
  // When buidling the boot image a particular page size is assumed, which may
  // not match the actual page size of the system.
  uint32_t kernel_image_size_;
  uint32_t kernel_image_offset_;
  uint32_t ramdisk_image_size_;
  uint32_t ramdisk_image_offset_;
  uint32_t vendor_ramdisk_image_size_;
  uint32_t vendor_ramdisk_image_offset_;

  // Extracts the kernel image to the given path
  bool ExtractKernelImage(const std::string& path) const;
  // Extracts the ramdisk image to the given path. It may return false if the
  // boot image does not contain a ramdisk, which is the case when having system
  // as root.
  bool ExtractRamdiskImage(const std::string& path) const;
  // Extracts the vendor ramdisk image to the given path
  bool ExtractVendorRamdiskImage(const std::string& path) const;
};

}  // namespace cuttlefish
