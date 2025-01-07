//
// Copyright (C) 2022 The Android Open Source Project
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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "host/commands/cvd/cli/selector/start_selector_parser.h"
#include "host/commands/cvd/cli/types.h"

namespace cuttlefish {
namespace selector {

struct ExpectedOutput {
  std::optional<std::vector<std::string>> names;
  std::optional<std::string> group_name;
  std::optional<std::vector<std::string>> per_instance_names;
};

struct InputOutput {
  std::string input;
  ExpectedOutput expected;
};

class ValidNamesTest : public testing::TestWithParam<InputOutput> {
 protected:
  ValidNamesTest();
  void Init();

  cvd_common::Args selector_args_;
  ExpectedOutput expected_output_;
};

class InvalidNamesTest : public testing::TestWithParam<std::string> {
 protected:
  InvalidNamesTest();

  cvd_common::Args selector_args_;
};

}  // namespace selector
}  // namespace cuttlefish
