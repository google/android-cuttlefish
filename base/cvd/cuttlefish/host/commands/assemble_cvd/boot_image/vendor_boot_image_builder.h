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

#pragma once

#include <memory>
#include <string>

#include "cuttlefish/io/concat.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class VendorBootImageBuilder {
 public:
  VendorBootImageBuilder& PageSize(uint32_t) &;
  VendorBootImageBuilder PageSize(uint32_t) &&;

  VendorBootImageBuilder& KernelAddr(uint32_t) &;
  VendorBootImageBuilder KernelAddr(uint32_t) &&;

  VendorBootImageBuilder& RamdiskAddr(uint32_t) &;
  VendorBootImageBuilder RamdiskAddr(uint32_t) &&;

  VendorBootImageBuilder& VendorRamdisk(std::unique_ptr<ReaderSeeker>) &;
  VendorBootImageBuilder VendorRamdisk(std::unique_ptr<ReaderSeeker>) &&;

  VendorBootImageBuilder& KernelCommandLine(std::string_view) &;
  VendorBootImageBuilder KernelCommandLine(std::string_view) &&;

  VendorBootImageBuilder& TagsAddr(uint32_t) &;
  VendorBootImageBuilder TagsAddr(uint32_t) &&;

  VendorBootImageBuilder& Name(std::string_view) &;
  VendorBootImageBuilder Name(std::string_view) &&;

  VendorBootImageBuilder& Dtb(std::unique_ptr<ReaderSeeker>) &;
  VendorBootImageBuilder Dtb(std::unique_ptr<ReaderSeeker>) &&;

  VendorBootImageBuilder& DtbAddr(uint64_t) &;
  VendorBootImageBuilder DtbAddr(uint64_t) &&;

  VendorBootImageBuilder& Bootconfig(std::unique_ptr<ReaderSeeker>) &;
  VendorBootImageBuilder Bootconfig(std::unique_ptr<ReaderSeeker>) &&;

  Result<ConcatReaderSeeker> BuildV4();

 private:
  // https://cs.android.com/android/platform/superproject/main/+/main:system/tools/mkbootimg/mkbootimg.py;l=517;drc=053c389f03f3c14f86b808608ccb5669ff8b887a
  static constexpr uint32_t kBaseAddress = 0x10000000;
  static constexpr uint32_t kKernelOffset = 0x8000;
  static constexpr uint32_t kRamdiskOffset = 0x1000000;
  static constexpr uint32_t kTagsOffset = 0x100;
  static constexpr uint64_t kDtbOffset = 0x01f00000;

  // https://cs.android.com/android/platform/superproject/main/+/main:system/tools/mkbootimg/mkbootimg.py;l=536;drc=053c389f03f3c14f86b808608ccb5669ff8b887a
  uint32_t page_size_ = 2048;
  uint32_t kernel_addr_ = kBaseAddress + kKernelOffset;
  uint32_t ramdisk_addr_ = kBaseAddress + kRamdiskOffset;
  std::unique_ptr<ReaderSeeker> vendor_ramdisk_;
  std::string kernel_command_line_;
  uint32_t tags_addr_ = kBaseAddress + kTagsOffset;
  std::string name_;
  std::unique_ptr<ReaderSeeker> dtb_;
  uint64_t dtb_addr_ = kBaseAddress + kDtbOffset;
  std::unique_ptr<ReaderSeeker> bootconfig_;
};

}  // namespace cuttlefish
