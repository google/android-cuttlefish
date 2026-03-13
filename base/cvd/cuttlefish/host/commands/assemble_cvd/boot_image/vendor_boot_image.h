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

#include <stdint.h>

#include <variant>

#include "bootimg.h"

#include "cuttlefish/io/io.h"
#include "cuttlefish/io/read_window_view.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class VendorBootImage {
 public:
  static Result<VendorBootImage> Read(std::unique_ptr<ReaderSeeker>);

  uint32_t PageSize() const;

  uint32_t KernelAddr() const;

  uint32_t RamdiskAddr() const;

  // All the vendor ramdisks concatenated together
  ReadWindowView VendorRamdisk() const;

  std::string KernelCommandLine() const;

  uint32_t TagsAddr() const;

  std::string Name() const;

  ReadWindowView Dtb() const;

  uint64_t DtbAddr() const;

  std::optional<ReadWindowView> Bootconfig() const;

 private:
  using HeaderVariant =
      std::variant<vendor_boot_img_hdr_v3, vendor_boot_img_hdr_v4>;

  VendorBootImage(std::unique_ptr<ReaderSeeker>, HeaderVariant);

  const vendor_boot_img_hdr_v3& AsV3() const;
  uint64_t VendorRamdiskBegin() const;
  uint64_t DtbBegin() const;

  std::unique_ptr<ReaderSeeker> reader_;
  HeaderVariant header_;
};

}  // namespace cuttlefish
