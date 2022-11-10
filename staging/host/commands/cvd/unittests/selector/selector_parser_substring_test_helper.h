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
#include <vector>

#include <gtest/gtest.h>

#include "host/commands/cvd/selector/selector_cmdline_parser.h"

namespace cuttlefish {
namespace selector {

using Envs = std::unordered_map<std::string, std::string>;
using Args = std::vector<std::string>;

struct SubstringTestInput {
  std::string input_args;
  bool expected;
};

class SubstringTest : public testing::TestWithParam<SubstringTestInput> {
 protected:
  SubstringTest();

  bool expected_result_;
  std::optional<SelectorFlagsParser> parser_;
};

}  // namespace selector
}  // namespace cuttlefish
