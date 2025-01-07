//
// Copyright (C) 2023 The Android Open Source Project
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

#include "common/libs/utils/files.h"

#include <fstream>
#include <string>

#include <android-base/file.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/libs/utils/files_test_helper.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/result_matchers.h"

namespace cuttlefish {

using testing::IsTrue;

void CreateTempFileWithText(const std::string& filepath,
                            const std::string& text) {
  std::ofstream file(filepath);
  file << text;
  file.close();
}

class FilesTests : public ::testing::Test {
 protected:
  Result<void> CreateTestDirs() {
    src_dir_ = std::string(temp_dir_.path) + "/device-imge-123.zip";
    CF_EXPECT(EnsureDirectoryExists(src_dir_, 0755));
    CreateTempFileWithText(src_dir_ + "/file1.txt", "file1");
    std::string sub_dir = src_dir_ + "/sub_dir";
    CF_EXPECT(EnsureDirectoryExists(sub_dir.c_str(), 0755));
    CreateTempFileWithText(sub_dir + "/file2.txt", "file2");
    dst_dir_ = std::string(temp_dir_.path) + "/target_dir";
    return {};
  }

  void SetUp() override {
    Result<void> result = CreateTestDirs();
    if (!result.ok()) {
      FAIL() << result.error().FormatForEnv();
    }
  }

  TemporaryDir temp_dir_;
  std::string src_dir_;
  std::string dst_dir_;
};

TEST_F(FilesTests, HardLinkRecursivelyFailsIfSourceIsNotADirectory) {
  Result<void> result = HardLinkDirecoryContentsRecursively(
      src_dir_ + "/file1.txt", dst_dir_ + "/file1.txt");

  EXPECT_THAT(result, IsError());
}

TEST_F(FilesTests, HardLinkRecursively) {
  Result<void> result = HardLinkDirecoryContentsRecursively(src_dir_, dst_dir_);

  EXPECT_THAT(result, IsOk());
  Result<bool> resultHardLinked =
      AreHardLinked(src_dir_ + "/file1.txt", dst_dir_ + "/file1.txt");
  EXPECT_THAT(resultHardLinked, IsOk());
  EXPECT_THAT(resultHardLinked.value(), IsTrue());
  resultHardLinked = AreHardLinked(src_dir_ + "/sub_dir/file2.txt",
                                   dst_dir_ + "/sub_dir/file2.txt");
  EXPECT_THAT(resultHardLinked, IsOk());
  EXPECT_THAT(resultHardLinked.value(), IsTrue());
}

TEST_F(FilesTests, MoveDirectoryContentsFailsIfSourceIsNotADirectory) {
  Result<void> result =
      MoveDirectoryContents(src_dir_ + "/file1.txt", dst_dir_ + "/file1.txt");
  EXPECT_THAT(result, IsError());
}

TEST_F(FilesTests, MoveDirectoryContents) {
  Result<void> result = MoveDirectoryContents(src_dir_, dst_dir_);
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(IsDirectoryEmpty(src_dir_), IsTrue());
  EXPECT_THAT(FileExists(dst_dir_ + "/file1.txt"), IsTrue());
  EXPECT_THAT(FileExists(dst_dir_ + "/sub_dir/file2.txt"), IsTrue());
}

TEST_P(EmulateAbsolutePathBase, NoHomeNoPwd) {
  const bool follow_symlink = false;
  auto emulated_absolute_path =
      EmulateAbsolutePath({.current_working_dir = std::nullopt,
                           .home_dir = std::nullopt,
                           .path_to_convert = input_path_,
                           .follow_symlink = follow_symlink});

  ASSERT_TRUE(emulated_absolute_path.ok())
      << emulated_absolute_path.error().Trace();
  ASSERT_EQ(*emulated_absolute_path, expected_path_);
}

INSTANTIATE_TEST_SUITE_P(
    CommonUtilsTest, EmulateAbsolutePathBase,
    testing::Values(InputOutput{.path_to_convert_ = "/", .expected_ = "/"},
                    InputOutput{.path_to_convert_ = "", .expected_ = ""},
                    InputOutput{.path_to_convert_ = "/a/b/c/",
                                .expected_ = "/a/b/c"},
                    InputOutput{.path_to_convert_ = "/a", .expected_ = "/a"}));

TEST_P(EmulateAbsolutePathWithPwd, NoHomeYesPwd) {
  const bool follow_symlink = false;
  auto emulated_absolute_path =
      EmulateAbsolutePath({.current_working_dir = current_dir_,
                           .home_dir = "/a/b/c",
                           .path_to_convert = input_path_,
                           .follow_symlink = follow_symlink});

  ASSERT_TRUE(emulated_absolute_path.ok())
      << emulated_absolute_path.error().Trace();
  ASSERT_EQ(*emulated_absolute_path, expected_path_);
}

INSTANTIATE_TEST_SUITE_P(
    CommonUtilsTest, EmulateAbsolutePathWithPwd,
    testing::Values(InputOutput{.path_to_convert_ = "",
                                .working_dir_ = "/x/y/z",
                                .expected_ = ""},
                    InputOutput{.path_to_convert_ = "a",
                                .working_dir_ = "/x/y/z",
                                .expected_ = "/x/y/z/a"},
                    InputOutput{.path_to_convert_ = ".",
                                .working_dir_ = "/x/y/z",
                                .expected_ = "/x/y/z"},
                    InputOutput{.path_to_convert_ = "..",
                                .working_dir_ = "/x/y/z",
                                .expected_ = "/x/y"},
                    InputOutput{.path_to_convert_ = "./k/../../t/./q",
                                .working_dir_ = "/x/y/z",
                                .expected_ = "/x/y/t/q"}));

TEST_P(EmulateAbsolutePathWithHome, YesHomeNoPwd) {
  const bool follow_symlink = false;
  auto emulated_absolute_path =
      EmulateAbsolutePath({.current_working_dir = std::nullopt,
                           .home_dir = home_dir_,
                           .path_to_convert = input_path_,
                           .follow_symlink = follow_symlink});

  ASSERT_TRUE(emulated_absolute_path.ok())
      << emulated_absolute_path.error().Trace();
  ASSERT_EQ(*emulated_absolute_path, expected_path_);
}

INSTANTIATE_TEST_SUITE_P(
    CommonUtilsTest, EmulateAbsolutePathWithHome,
    testing::Values(InputOutput{.path_to_convert_ = "~",
                                .home_dir_ = "/x/y/z",
                                .expected_ = "/x/y/z"},
                    InputOutput{.path_to_convert_ = "~/a",
                                .home_dir_ = "/x/y/z",
                                .expected_ = "/x/y/z/a"},
                    InputOutput{.path_to_convert_ = "~/.",
                                .home_dir_ = "/x/y/z",
                                .expected_ = "/x/y/z"},
                    InputOutput{.path_to_convert_ = "~/..",
                                .home_dir_ = "/x/y/z",
                                .expected_ = "/x/y"},
                    InputOutput{.path_to_convert_ = "~/k/../../t/./q",
                                .home_dir_ = "/x/y/z",
                                .expected_ = "/x/y/t/q"}));

}  // namespace cuttlefish
