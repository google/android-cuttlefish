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

#include "host/commands/cvd/unittests/server/common_utils_helper.h"

namespace cuttlefish {

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
    testing::Values(InputOutput{.working_dir_ = "/x/y/z",
                                .path_to_convert_ = "",
                                .expected_ = ""},
                    InputOutput{.working_dir_ = "/x/y/z",
                                .path_to_convert_ = "a",
                                .expected_ = "/x/y/z/a"},
                    InputOutput{.working_dir_ = "/x/y/z",
                                .path_to_convert_ = ".",
                                .expected_ = "/x/y/z"},
                    InputOutput{.working_dir_ = "/x/y/z",
                                .path_to_convert_ = "..",
                                .expected_ = "/x/y"},
                    InputOutput{.working_dir_ = "/x/y/z",
                                .path_to_convert_ = "./k/../../t/./q",
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
    testing::Values(InputOutput{.home_dir_ = "/x/y/z",
                                .path_to_convert_ = "~",
                                .expected_ = "/x/y/z"},
                    InputOutput{.home_dir_ = "/x/y/z",
                                .path_to_convert_ = "~/a",
                                .expected_ = "/x/y/z/a"},
                    InputOutput{.home_dir_ = "/x/y/z",
                                .path_to_convert_ = "~/.",
                                .expected_ = "/x/y/z"},
                    InputOutput{.home_dir_ = "/x/y/z",
                                .path_to_convert_ = "~/..",
                                .expected_ = "/x/y"},
                    InputOutput{.home_dir_ = "/x/y/z",
                                .path_to_convert_ = "~/k/../../t/./q",
                                .expected_ = "/x/y/t/q"}));

}  // namespace cuttlefish
