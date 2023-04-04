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

#pragma once

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "host/commands/cvd/common_utils.h"

namespace cuttlefish {

struct InputOutput {
  std::string path_to_convert_;
  std::string working_dir_;
  std::string home_dir_;
  std::string expected_;
};

class EmulateAbsolutePathBase : public testing::TestWithParam<InputOutput> {
 protected:
  EmulateAbsolutePathBase();

  std::string input_path_;
  std::string expected_path_;
};

class EmulateAbsolutePathWithPwd : public testing::TestWithParam<InputOutput> {
 protected:
  EmulateAbsolutePathWithPwd();

  std::string input_path_;
  std::string current_dir_;
  std::string expected_path_;
};

class EmulateAbsolutePathWithHome : public EmulateAbsolutePathBase {
 protected:
  EmulateAbsolutePathWithHome();

  std::string input_path_;
  std::string home_dir_;
  std::string expected_path_;
};

}  // namespace cuttlefish
