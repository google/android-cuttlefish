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

#include "cuttlefish/host/commands/assemble_cvd/boot_image/vendor_boot_image.h"

#include <utility>
#include <variant>

#include "absl/log/log.h"
#include "bootimg.h"

#include "cuttlefish/io/io.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

// https://source.android.com/docs/core/architecture/partitions/vendor-boot-partitions

static_assert(sizeof(vendor_boot_img_hdr_v4) >= sizeof(vendor_boot_img_hdr_v3));

/* static */ Result<VendorBootImage> VendorBootImage::Read(
    std::unique_ptr<ReaderSeeker> rd) {
  // `magic` and `header_version` are always in the same place, v4 extends v3
  auto v4 = CF_EXPECT(PReadExactBinary<vendor_boot_img_hdr_v4>(*rd, 0));
  CF_EXPECT_EQ(memcmp(v4.magic, VENDOR_BOOT_MAGIC, VENDOR_BOOT_MAGIC_SIZE), 0);

  switch (v4.header_version) {
    case 3:
      return VendorBootImage(std::move(rd),
                             *static_cast<vendor_boot_img_hdr_v3*>(&v4));
    case 4:
      return VendorBootImage(std::move(rd), v4);
    default:
      return CF_ERRF("Unknown header version '{}'", v4.header_version);
  }
}

uint32_t VendorBootImage::PageSize() const { return AsV3().page_size; }

uint32_t VendorBootImage::KernelAddr() const { return AsV3().kernel_addr; }

uint32_t VendorBootImage::RamdiskAddr() const { return AsV3().ramdisk_addr; }

ReadWindowView VendorBootImage::VendorRamdisk() const {
  const uint64_t begin = VendorRamdiskBegin();
  const uint32_t size = AsV3().vendor_ramdisk_size;
  return ReadWindowView(*reader_, begin, size);
}

std::string VendorBootImage::KernelCommandLine() const {
  return reinterpret_cast<const char*>(AsV3().cmdline);
}

uint32_t VendorBootImage::TagsAddr() const { return AsV3().tags_addr; }

std::string VendorBootImage::Name() const {
  return reinterpret_cast<const char*>(AsV3().name);
}

ReadWindowView VendorBootImage::Dtb() const {
  const uint64_t begin = DtbBegin();
  const uint32_t size = AsV3().dtb_size;
  return ReadWindowView(*reader_, begin, size);
}

uint64_t VendorBootImage::DtbAddr() const { return AsV3().dtb_addr; }

std::optional<ReadWindowView> VendorBootImage::Bootconfig() const {
  auto v4 = std::get_if<vendor_boot_img_hdr_v4>(&header_);
  if (!v4) {
    return std::nullopt;
  }
  // https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/android16-qpr2-release/include/bootimg/bootimg.h#359
  const uint32_t page_size = v4->page_size;
  const uint32_t dtb_pages = (v4->dtb_size + page_size - 1) / page_size;
  const uint32_t table_pages =
      (v4->vendor_ramdisk_table_size + page_size - 1) / page_size;
  const uint64_t bootconfig_begin =
      DtbBegin() + (dtb_pages + table_pages) * page_size;
  return ReadWindowView(*reader_, bootconfig_begin, v4->bootconfig_size);
}

VendorBootImage::VendorBootImage(std::unique_ptr<ReaderSeeker> reader,
                                 HeaderVariant header)
    : reader_(std::move(reader)), header_(std::move(header)) {}

const vendor_boot_img_hdr_v3& VendorBootImage::AsV3() const {
  if (auto v3 = std::get_if<vendor_boot_img_hdr_v3>(&header_); v3) {
    return *v3;
  } else if (auto v4 = std::get_if<vendor_boot_img_hdr_v4>(&header_); v4) {
    return *v4;
  } else {
    LOG(FATAL) << "Variant is in an invalid state";
  }
}

uint64_t VendorBootImage::VendorRamdiskBegin() const {
  const auto visitor = [](const auto& hdr) { return sizeof(hdr); };
  const uint64_t header_size = std::visit(visitor, header_);
  const uint32_t page_size = AsV3().page_size;
  const uint64_t header_pages = (header_size + page_size - 1) / page_size;
  return header_pages * page_size;
}

uint64_t VendorBootImage::DtbBegin() const {
  const uint32_t page_size = AsV3().page_size;
  const uint32_t vendor_ramdisk_pages =
      (AsV3().vendor_ramdisk_size + page_size - 1) / page_size;
  return VendorRamdiskBegin() + (vendor_ramdisk_pages * page_size);
}

}  // namespace cuttlefish
