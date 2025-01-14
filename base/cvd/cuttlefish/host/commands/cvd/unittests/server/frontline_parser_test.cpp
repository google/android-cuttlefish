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

#include <iostream>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "common/libs/utils/result_matchers.h"
#include "host/commands/cvd/cli/frontline_parser.h"

namespace cuttlefish {

TEST(FrontlineParserTest, SelectorArgs) {
  cvd_common::Args input{"cvd", "--instance_name=1", "status"};

  auto result = ExtractCvdArgs(input);

  EXPECT_THAT(result, IsOk());
  ASSERT_EQ(*result, std::vector<std::string>{"--instance_name=1"});
  ASSERT_EQ(input, (std::vector<std::string>{"cvd", "status"}));
}

}  // namespace cuttlefish
