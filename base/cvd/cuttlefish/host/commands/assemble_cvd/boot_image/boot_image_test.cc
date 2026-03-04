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

#include "cuttlefish/host/commands/assemble_cvd/boot_image/boot_image.h"

#include <memory>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/commands/assemble_cvd/boot_image/boot_image_builder.h"
#include "cuttlefish/io/concat.h"
#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/read_window_view.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(BootImageTest, AllFieldsSet) {
  static constexpr uint32_t kOsVersion = 123;
  static constexpr std::string_view kKernelCmdLine = "kernel cmd line";
  static constexpr std::string_view kKernel = "kernel";
  static constexpr std::string_view kRamdisk = "ramdisk";
  static constexpr std::string_view kSignature = "signature";

  Result<ConcatReaderSeeker> image = BootImageBuilder()
                                         .OsVersion(kOsVersion)
                                         .KernelCommandLine(kKernelCmdLine)
                                         .Kernel(InMemoryIo(kKernel))
                                         .Ramdisk(InMemoryIo(kRamdisk))
                                         .Signature(InMemoryIo(kSignature))
                                         .BuildV4();
  ASSERT_THAT(image, IsOk());

  Result<BootImage> parsed =
      BootImage::Read(std::make_unique<ConcatReaderSeeker>(std::move(*image)));
  ASSERT_THAT(parsed, IsOk());

  EXPECT_EQ(parsed->OsVersion(), kOsVersion);
  EXPECT_EQ(parsed->KernelCommandLine(), kKernelCmdLine);

  ReadWindowView kernel = parsed->Kernel();
  EXPECT_THAT(ReadToString(kernel), IsOkAndValue(kKernel));

  ReadWindowView ramdisk = parsed->Ramdisk();
  EXPECT_THAT(ReadToString(ramdisk), IsOkAndValue(kRamdisk));

  std::optional<ReadWindowView> signature = parsed->Signature();
  ASSERT_TRUE(signature.has_value());
  EXPECT_THAT(ReadToString(*signature), IsOkAndValue(kSignature));
}

TEST(BootImageTest, AllFieldsEmpty) {
  Result<ConcatReaderSeeker> image = BootImageBuilder().BuildV4();
  ASSERT_THAT(image, IsOk());

  Result<BootImage> parsed =
      BootImage::Read(std::make_unique<ConcatReaderSeeker>(std::move(*image)));
  ASSERT_THAT(parsed, IsOk());

  EXPECT_EQ(parsed->OsVersion(), 0);
  EXPECT_EQ(parsed->KernelCommandLine(), "");

  ReadWindowView kernel = parsed->Kernel();
  EXPECT_THAT(ReadToString(kernel), IsOkAndValue(""));

  ReadWindowView ramdisk = parsed->Ramdisk();
  EXPECT_THAT(ReadToString(ramdisk), IsOkAndValue(""));

  std::optional<ReadWindowView> signature = parsed->Signature();
  ASSERT_TRUE(signature.has_value());
  EXPECT_THAT(ReadToString(*signature), IsOkAndValue(""));
}

TEST(BootImageTest, AllFieldsLongValue) {
  std::string over_one_page(kBootImagePageSize + 1, ' ');

  Result<ConcatReaderSeeker> image = BootImageBuilder()
                                         .Kernel(InMemoryIo(over_one_page))
                                         .Ramdisk(InMemoryIo(over_one_page))
                                         .Signature(InMemoryIo(over_one_page))
                                         .BuildV4();
  ASSERT_THAT(image, IsOk());

  Result<BootImage> parsed =
      BootImage::Read(std::make_unique<ConcatReaderSeeker>(std::move(*image)));
  ASSERT_THAT(parsed, IsOk());

  ReadWindowView kernel = parsed->Kernel();
  EXPECT_THAT(ReadToString(kernel), IsOkAndValue(over_one_page));

  ReadWindowView ramdisk = parsed->Ramdisk();
  EXPECT_THAT(ReadToString(ramdisk), IsOkAndValue(over_one_page));

  std::optional<ReadWindowView> signature = parsed->Signature();
  ASSERT_TRUE(signature.has_value());
  EXPECT_THAT(ReadToString(*signature), IsOkAndValue(over_one_page));
}

TEST(BootImageTest, KernelCommandLineTooLong) {
  Result<ConcatReaderSeeker> image =
      BootImageBuilder()
          .KernelCommandLine(std::string(kBootImagePageSize, ' '))
          .BuildV4();
  EXPECT_THAT(image, IsError());
}

}  // namespace
}  // namespace cuttlefish
