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

#include "host/commands/cvd/unittests/selector/parser_names_helper.h"

#include <android-base/strings.h>
#include <gtest/gtest.h>

#include "host/commands/cvd/selector/selector_option_parser_utils.h"

namespace cuttlefish {
namespace selector {

ValidNamesTest::ValidNamesTest() { Init(); }

void ValidNamesTest::Init() {
  auto [input, expected_output] = GetParam();
  selector_args_ = android::base::Tokenize(input, " ");
  expected_output_ = std::move(expected_output);
}

InvalidNamesTest::InvalidNamesTest() {
  selector_args_ = android::base::Tokenize(GetParam(), " ");
}

}  // namespace selector
}  // namespace cuttlefish
