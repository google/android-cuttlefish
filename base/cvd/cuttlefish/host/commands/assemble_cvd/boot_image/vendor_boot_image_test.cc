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

#include <memory>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/commands/assemble_cvd/boot_image/vendor_boot_image_builder.h"
#include "cuttlefish/io/concat.h"
#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/read_window_view.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(VendorBootImageTest, AllFieldsSet) {
  static constexpr uint32_t kPageSize = 4096;
  static constexpr uint32_t kKernelAddr = 1;
  static constexpr uint32_t kRamdiskAddr = 2;
  static constexpr std::string_view kVendorRamdisk = "vendor ramdisk";
  static constexpr std::string_view kKernelCommandLine = "kernel cmdline";
  static constexpr uint32_t kTagsAddr = 3;
  static constexpr std::string_view kName = "name";
  static constexpr std::string_view kDtb = "dtb";
  static constexpr uint64_t kDtbAddr = 4;
  static constexpr std::string_view kBootconfig = "bootconfig";

  Result<ConcatReaderSeeker> image =
      VendorBootImageBuilder()
          .PageSize(kPageSize)
          .KernelAddr(kKernelAddr)
          .RamdiskAddr(kRamdiskAddr)
          .VendorRamdisk(InMemoryIo(kVendorRamdisk))
          .KernelCommandLine(kKernelCommandLine)
          .TagsAddr(kTagsAddr)
          .Name(kName)
          .Dtb(InMemoryIo(kDtb))
          .DtbAddr(kDtbAddr)
          .Bootconfig(InMemoryIo(kBootconfig))
          .BuildV4();
  ASSERT_THAT(image, IsOk());

  Result<VendorBootImage> parsed = VendorBootImage::Read(
      std::make_unique<ConcatReaderSeeker>(std::move(*image)));
  ASSERT_THAT(parsed, IsOk());

  EXPECT_EQ(parsed->PageSize(), kPageSize);
  EXPECT_EQ(parsed->KernelAddr(), kKernelAddr);
  EXPECT_EQ(parsed->RamdiskAddr(), kRamdiskAddr);

  ReadWindowView vendor_ramdisk = parsed->VendorRamdisk();
  EXPECT_THAT(ReadToString(vendor_ramdisk), IsOkAndValue(kVendorRamdisk));

  EXPECT_EQ(parsed->KernelCommandLine(), kKernelCommandLine);
  EXPECT_EQ(parsed->TagsAddr(), kTagsAddr);
  EXPECT_EQ(parsed->Name(), kName);

  ReadWindowView dtb = parsed->Dtb();
  EXPECT_THAT(ReadToString(dtb), IsOkAndValue(kDtb));

  EXPECT_EQ(parsed->DtbAddr(), kDtbAddr);

  std::optional<ReadWindowView> bootconfig = parsed->Bootconfig();
  ASSERT_TRUE(bootconfig.has_value());
  EXPECT_THAT(ReadToString(*bootconfig), IsOkAndValue(kBootconfig));
}

TEST(VendorBootImageTest, NoFieldsSet) {
  Result<ConcatReaderSeeker> image = VendorBootImageBuilder().BuildV4();
  ASSERT_THAT(image, IsOk());

  Result<VendorBootImage> parsed = VendorBootImage::Read(
      std::make_unique<ConcatReaderSeeker>(std::move(*image)));
  ASSERT_THAT(parsed, IsOk());

  EXPECT_EQ(parsed->PageSize(), VendorBootImageBuilder::kDefaultPageSize);
  EXPECT_EQ(parsed->KernelAddr(),
            VendorBootImageBuilder::kDefaultBaseAddress +
                VendorBootImageBuilder::kDefaultKernelOffset);
  EXPECT_EQ(parsed->RamdiskAddr(),
            VendorBootImageBuilder::kDefaultBaseAddress +
                VendorBootImageBuilder::kDefaultRamdiskOffset);

  ReadWindowView vendor_ramdisk = parsed->VendorRamdisk();
  EXPECT_THAT(ReadToString(vendor_ramdisk), IsOkAndValue(""));

  EXPECT_EQ(parsed->KernelCommandLine(), "");
  EXPECT_EQ(parsed->TagsAddr(), VendorBootImageBuilder::kDefaultBaseAddress +
                                    VendorBootImageBuilder::kDefaultTagsOffset);
  EXPECT_EQ(parsed->Name(), "");

  ReadWindowView dtb = parsed->Dtb();
  EXPECT_THAT(ReadToString(dtb), IsOkAndValue(""));

  EXPECT_EQ(parsed->DtbAddr(), VendorBootImageBuilder::kDefaultBaseAddress +
                                   VendorBootImageBuilder::kDefaultDtbOffset);

  std::optional<ReadWindowView> bootconfig = parsed->Bootconfig();
  ASSERT_TRUE(bootconfig.has_value());
  EXPECT_THAT(ReadToString(*bootconfig), IsOkAndValue(""));
}

}  // namespace
}  // namespace cuttlefish
