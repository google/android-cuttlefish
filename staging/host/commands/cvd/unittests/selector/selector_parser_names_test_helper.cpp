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

#include "host/commands/cvd/unittests/selector/selector_parser_names_test_helper.h"

#include <sys/types.h>
#include <unistd.h>

#include <android-base/strings.h>
#include <gtest/gtest.h>

#include "host/commands/cvd/selector/selector_cmdline_parser.h"
#include "host/commands/cvd/selector/selector_option_parser_utils.h"

namespace cuttlefish {
namespace selector {

ValidNamesTest::ValidNamesTest() { Init(); }

void ValidNamesTest::Init() {
  const uid_t uid = getuid();
  auto [input, expected_output] = GetParam();
  selector_args_ = android::base::Tokenize(input, " ");
  expected_output_ = std::move(expected_output);
  auto parse_result = StartSelectorParser::ConductSelectFlagsParser(
      uid, selector_args_, Args{}, Envs{});
  if (parse_result.ok()) {
    parser_ = std::move(*parse_result);
  }
}

InvalidNamesTest::InvalidNamesTest() {
  const uid_t uid = getuid();
  auto input = GetParam();
  auto selector_args = android::base::Tokenize(input, " ");
  auto parse_result = StartSelectorParser::ConductSelectFlagsParser(
      uid, selector_args, Args{}, Envs{});
  if (parse_result.ok()) {
    parser_ = std::move(*parse_result);
  }
}

}  // namespace selector
}  // namespace cuttlefish
