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

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/commands/assemble_cvd/boot_image/boot_image_builder.h"
#include "cuttlefish/io/concat.h"
#include "cuttlefish/io/owning_in_memory_io.h"
#include "cuttlefish/io/read_window_view.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(BootImageTest, AllFieldsSet) {
  static constexpr uint32_t kOsVersion = 123;
  static constexpr std::string_view kKernelCmdLine = "kernel cmd line";
  static constexpr std::string_view kKernel = "kernel";
  static constexpr std::string_view kRamdisk = "ramdisk";
  static constexpr std::string_view kSignature = "signature";

  ConcatReaderSeeker image =
      CF_ASSERT(BootImageBuilder()
                    .OsVersion(kOsVersion)
                    .KernelCommandLine(kKernelCmdLine)
                    .Kernel(std::make_unique<OwningInMemoryIo>(kKernel))
                    .Ramdisk(std::make_unique<OwningInMemoryIo>(kRamdisk))
                    .Signature(std::make_unique<OwningInMemoryIo>(kSignature))
                    .BuildV4());

  BootImage parsed = CF_ASSERT(
      BootImage::Read(std::make_unique<ConcatReaderSeeker>(std::move(image))));

  EXPECT_EQ(parsed.OsVersion(), kOsVersion);
  EXPECT_EQ(parsed.KernelCommandLine(), kKernelCmdLine);
  EXPECT_THAT(ReadToString(parsed.Kernel()), IsOkAndValue(kKernel));
  EXPECT_THAT(ReadToString(parsed.Ramdisk()), IsOkAndValue(kRamdisk));

  std::optional<ReadWindowView> signature = parsed.Signature();
  ASSERT_TRUE(signature.has_value());
  EXPECT_THAT(ReadToString(*signature), IsOkAndValue(kSignature));
}

TEST(BootImageTest, AllFieldsEmpty) {
  ConcatReaderSeeker image = CF_ASSERT(BootImageBuilder().BuildV4());

  BootImage parsed = CF_ASSERT(
      BootImage::Read(std::make_unique<ConcatReaderSeeker>(std::move(image))));

  EXPECT_EQ(parsed.OsVersion(), 0);
  EXPECT_EQ(parsed.KernelCommandLine(), "");

  EXPECT_THAT(ReadToString(parsed.Kernel()), IsOkAndValue(""));
  EXPECT_THAT(ReadToString(parsed.Ramdisk()), IsOkAndValue(""));

  std::optional<ReadWindowView> signature = parsed.Signature();
  ASSERT_TRUE(signature.has_value());
  EXPECT_THAT(ReadToString(*signature), IsOkAndValue(""));
}

TEST(BootImageTest, AllFieldsLongValue) {
  std::string over_one_page(kBootImagePageSize + 1, ' ');

  ConcatReaderSeeker image = CF_ASSERT(
      BootImageBuilder()
          .Kernel(std::make_unique<OwningInMemoryIo>(over_one_page))
          .Ramdisk(std::make_unique<OwningInMemoryIo>(over_one_page))
          .Signature(std::make_unique<OwningInMemoryIo>(over_one_page))
          .BuildV4());

  BootImage parsed = CF_ASSERT(
      BootImage::Read(std::make_unique<ConcatReaderSeeker>(std::move(image))));

  EXPECT_THAT(ReadToString(parsed.Kernel()), IsOkAndValue(over_one_page));
  EXPECT_THAT(ReadToString(parsed.Ramdisk()), IsOkAndValue(over_one_page));

  std::optional<ReadWindowView> signature = parsed.Signature();
  ASSERT_TRUE(signature.has_value());
  EXPECT_THAT(ReadToString(*signature), IsOkAndValue(over_one_page));
}

TEST(BootImageTest, KernelCommandLineTooLong) {
  EXPECT_THAT(BootImageBuilder()
                  .KernelCommandLine(std::string(kBootImagePageSize, ' '))
                  .BuildV4(),
              IsError());
}

}  // namespace
}  // namespace cuttlefish
