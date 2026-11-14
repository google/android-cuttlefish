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

#include "cuttlefish/host/commands/assemble_cvd/system_image_dir_path_resolution.h"

#include <sys/stat.h>

#include <cstddef>
#include <fstream>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;

class SystemImageDirPathResolutionTest : public ::testing::Test {
 protected:
  void SetUp() override { system_image_dir_ = temp_dir_.path; }

  // Creates an empty file at `<system_image_dir>/<relative_path>`, making any
  // intermediate directories on the way.
  std::string CreateImageFile(std::string_view relative_path) {
    std::string path = absl::StrCat(system_image_dir_, "/", relative_path);
    for (size_t i = system_image_dir_.size() + 1; i < path.size(); ++i) {
      if (path[i] == '/') {
        mkdir(path.substr(0, i).c_str(), 0755);
      }
    }
    std::ofstream(path) << "not empty";
    return path;
  }

  TemporaryDir temp_dir_;
  std::string system_image_dir_;
};

TEST_F(SystemImageDirPathResolutionTest,
       ResolveSystemImageDirPath_EmptyValueStaysEmpty) {
  EXPECT_THAT(
      ResolveSystemImageDirPath("", system_image_dir_, "crosvm_acpi_table"),
      IsOkAndValue(Eq("")));
}

TEST_F(SystemImageDirPathResolutionTest,
       ResolveCrosvmDeviceTreeOverlayPath_EmptyValueStaysEmpty) {
  EXPECT_THAT(ResolveCrosvmDeviceTreeOverlayPath("", system_image_dir_),
              IsOkAndValue(Eq("")));
}

TEST_F(SystemImageDirPathResolutionTest,
       ResolveCrosvmFileBackedMappingPath_EmptyValueStaysEmpty) {
  EXPECT_THAT(ResolveCrosvmFileBackedMappingPath("", system_image_dir_),
              IsOkAndValue(Eq("")));
}

TEST_F(SystemImageDirPathResolutionTest,
       RelativePathIsJoinedWithSystemImageDir) {
  const std::string expected = CreateImageFile("product/etc/opendice.aml");

  EXPECT_THAT(ResolveSystemImageDirPath("product/etc/opendice.aml",
                                        system_image_dir_, "crosvm_acpi_table"),
              IsOkAndValue(Eq(expected)));
}

TEST_F(SystemImageDirPathResolutionTest, JoinInsertsExactlyOneSeparator) {
  // --system_image_dir carries no trailing separator, so the join has to
  // supply exactly one: neither "<dir>opendice.aml" nor "<dir>//opendice.aml".
  ASSERT_NE(system_image_dir_.back(), '/');
  CreateImageFile("opendice.aml");

  EXPECT_THAT(ResolveSystemImageDirPath("opendice.aml", system_image_dir_,
                                        "crosvm_acpi_table"),
              IsOkAndValue(Eq(system_image_dir_ + "/opendice.aml")));
}

TEST_F(SystemImageDirPathResolutionTest, AbsolutePathIsNotRewritten) {
  // Deliberately a path that does not exist: an absolute value is the caller's
  // own choice, so it is neither rewritten nor newly existence-checked.
  EXPECT_THAT(ResolveSystemImageDirPath("/abs/opendice.aml", system_image_dir_,
                                        "crosvm_acpi_table"),
              IsOkAndValue(Eq("/abs/opendice.aml")));
}

TEST_F(SystemImageDirPathResolutionTest,
       MissingRelativeFileFailsNamingResolvedPath) {
  const std::string resolved = absl::StrCat(system_image_dir_, "/missing.aml");

  EXPECT_THAT(ResolveSystemImageDirPath("missing.aml", system_image_dir_,
                                        "crosvm_acpi_table"),
              IsErrorAndMessage(HasSubstr(resolved)));
  EXPECT_THAT(ResolveSystemImageDirPath("missing.aml", system_image_dir_,
                                        "crosvm_acpi_table"),
              IsErrorAndMessage(HasSubstr("crosvm_acpi_table")));
}

TEST_F(SystemImageDirPathResolutionTest, DeviceTreeOverlayResolvesBarePath) {
  const std::string expected = CreateImageFile("my.dtbo");

  EXPECT_THAT(ResolveCrosvmDeviceTreeOverlayPath("my.dtbo", system_image_dir_),
              IsOkAndValue(Eq(expected)));
}

TEST_F(SystemImageDirPathResolutionTest,
       DeviceTreeOverlayPreservesFilterSuffix) {
  const std::string expected = CreateImageFile("my.dtbo");

  EXPECT_THAT(
      ResolveCrosvmDeviceTreeOverlayPath("my.dtbo,filter", system_image_dir_),
      IsOkAndValue(Eq(absl::StrCat(expected, ",filter"))));
}

TEST_F(SystemImageDirPathResolutionTest,
       DeviceTreeOverlayPreservesInnerCommas) {
  const std::string expected = CreateImageFile("my.dtbo");

  // `select-symbols=[a,b]` legally contains commas, so the suffix must be
  // carried over verbatim rather than split and rejoined.
  EXPECT_THAT(
      ResolveCrosvmDeviceTreeOverlayPath("my.dtbo,filter,select-symbols=[a,b]",
                                         system_image_dir_),
      IsOkAndValue(Eq(absl::StrCat(expected, ",filter,select-symbols=[a,b]"))));
}

TEST_F(SystemImageDirPathResolutionTest,
       DeviceTreeOverlayAbsolutePathIsNotRewritten) {
  EXPECT_THAT(ResolveCrosvmDeviceTreeOverlayPath("/abs/my.dtbo,filter",
                                                 system_image_dir_),
              IsOkAndValue(Eq("/abs/my.dtbo,filter")));
}

TEST_F(SystemImageDirPathResolutionTest,
       DeviceTreeOverlayKeyedAbsolutePathIsNotRewritten) {
  // crosvm accepts the path under its own key as well, and such a value works
  // today, so it has to come out byte-identical.
  EXPECT_THAT(ResolveCrosvmDeviceTreeOverlayPath("path=/abs/my.dtbo,filter",
                                                 system_image_dir_),
              IsOkAndValue(Eq("path=/abs/my.dtbo,filter")));
}

TEST_F(SystemImageDirPathResolutionTest,
       DeviceTreeOverlayResolvesKeyedRelativePath) {
  const std::string expected = CreateImageFile("my.dtbo");

  // Only what follows `path=` is a path; the key and the suffix stay put.
  EXPECT_THAT(
      ResolveCrosvmDeviceTreeOverlayPath(
          "path=my.dtbo,filter,select-symbols=[a,b]", system_image_dir_),
      IsOkAndValue(
          Eq(absl::StrCat("path=", expected, ",filter,select-symbols=[a,b]"))));
}

TEST_F(SystemImageDirPathResolutionTest,
       DeviceTreeOverlayKeyedMissingFileFails) {
  const std::string resolved = absl::StrCat(system_image_dir_, "/missing.dtbo");

  EXPECT_THAT(ResolveCrosvmDeviceTreeOverlayPath("path=missing.dtbo,filter",
                                                 system_image_dir_),
              IsErrorAndMessage(HasSubstr(resolved)));
}

TEST_F(SystemImageDirPathResolutionTest,
       DeviceTreeOverlayOtherLeadingKeyIsUntouched) {
  // With a different key first, which token holds the path is a guess, so the
  // whole value is passed through: no rewrite and no existence check, even
  // though `my.dtbo` does not exist under the system image directory.
  EXPECT_THAT(ResolveCrosvmDeviceTreeOverlayPath(
                  "select-symbols=[mydev],path=my.dtbo", system_image_dir_),
              IsOkAndValue(Eq("select-symbols=[mydev],path=my.dtbo")));
}

TEST_F(SystemImageDirPathResolutionTest, DeviceTreeOverlayMissingFileFails) {
  const std::string resolved = absl::StrCat(system_image_dir_, "/missing.dtbo");

  EXPECT_THAT(ResolveCrosvmDeviceTreeOverlayPath("missing.dtbo,filter",
                                                 system_image_dir_),
              IsErrorAndMessage(HasSubstr(resolved)));
}

TEST_F(SystemImageDirPathResolutionTest,
       FileBackedMappingRewritesOnlyThePathToken) {
  const std::string expected = CreateImageFile("dice_handover_instance1");

  EXPECT_THAT(ResolveCrosvmFileBackedMappingPath(
                  "path=dice_handover_instance1,addr=0x9D1C3000,size=0x10000",
                  system_image_dir_),
              IsOkAndValue(Eq(absl::StrCat("path=", expected,
                                           ",addr=0x9D1C3000,size=0x10000"))));
}

TEST_F(SystemImageDirPathResolutionTest, FileBackedMappingPreservesTokenOrder) {
  const std::string expected = CreateImageFile("dice_handover_instance1");

  // crosvm's token order is free, so `path=` may appear last; the other
  // properties keep both their values and their positions.
  EXPECT_THAT(
      ResolveCrosvmFileBackedMappingPath(
          "addr=0x9D1C3000,size=0x10000,rw,path=dice_handover_instance1",
          system_image_dir_),
      IsOkAndValue(
          Eq(absl::StrCat("addr=0x9D1C3000,size=0x10000,rw,path=", expected))));
}

TEST_F(SystemImageDirPathResolutionTest,
       FileBackedMappingAbsolutePathIsNotRewritten) {
  EXPECT_THAT(ResolveCrosvmFileBackedMappingPath(
                  "path=/abs/dice_handover,addr=0x9D1C3000,size=0x10000",
                  system_image_dir_),
              IsOkAndValue(Eq("path=/abs/dice_handover,addr=0x9D1C3000,size="
                              "0x10000")));
}

TEST_F(SystemImageDirPathResolutionTest,
       FileBackedMappingWithoutPathTokenIsUntouched) {
  // crosvm also accepts the path positionally. There is no `path=` token to
  // rewrite, so the value must reach crosvm exactly as written.
  EXPECT_THAT(ResolveCrosvmFileBackedMappingPath(
                  "/dev/mem,addr=0x1000,size=0x2000", system_image_dir_),
              IsOkAndValue(Eq("/dev/mem,addr=0x1000,size=0x2000")));
}

TEST_F(SystemImageDirPathResolutionTest,
       FileBackedMappingWithoutPathTokenIsUntouchedWhenRelative) {
  // Same pass-through rule, but with a leading positional token that is
  // *relative*, which is the only shape that tells passing the value through
  // apart from resolving it. `some_relative_file` is deliberately never
  // created: pass-through performs no existence check either, so the call has
  // to succeed and hand back the value byte-for-byte.
  ASSERT_FALSE(
      std::ifstream(absl::StrCat(system_image_dir_, "/some_relative_file"))
          .good());

  EXPECT_THAT(
      ResolveCrosvmFileBackedMappingPath(
          "some_relative_file,addr=0x1000,size=0x2000", system_image_dir_),
      IsOkAndValue(Eq("some_relative_file,addr=0x1000,size=0x2000")));
}

TEST_F(SystemImageDirPathResolutionTest, FileBackedMappingMissingFileFails) {
  const std::string resolved = absl::StrCat(system_image_dir_, "/missing.bin");

  EXPECT_THAT(
      ResolveCrosvmFileBackedMappingPath(
          "path=missing.bin,addr=0x9D1C3000,size=0x10000", system_image_dir_),
      IsErrorAndMessage(HasSubstr(resolved)));
}

}  // namespace
}  // namespace cuttlefish
