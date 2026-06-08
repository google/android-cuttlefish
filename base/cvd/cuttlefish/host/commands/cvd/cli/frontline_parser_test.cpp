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

#include "cuttlefish/host/commands/cvd/cli/frontline_parser.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

struct ExtractCvdArgsTestParams {
  cvd_common::Args input_args;
  cvd_common::Args expected_args;
  selector::SelectorOptions expected_options;
};

class FrontlineParserTestFixture
    : public ::testing::TestWithParam<ExtractCvdArgsTestParams> {};

TEST_P(FrontlineParserTestFixture, FrontlineParserTest) {
  cvd_common::Args args = GetParam().input_args;

  Result<selector::SelectorOptions> options_result = ExtractCvdArgs(args);

  EXPECT_THAT(options_result, IsOk());
  EXPECT_EQ(*options_result, GetParam().expected_options);
  EXPECT_EQ(args, GetParam().expected_args);
}

INSTANTIATE_TEST_SUITE_P(SelectNoArgs, FrontlineParserTestFixture,
                         ::testing::Values(ExtractCvdArgsTestParams{
                             .input_args = {"cvd", "subcmd"},
                             .expected_args = {"cvd", "subcmd"},
                             .expected_options = selector::SelectorOptions{
                                 .group_name = std::nullopt,
                                 .instance_names = std::nullopt}}));

INSTANTIATE_TEST_SUITE_P(SelectNoSubCmdNoSelectors, FrontlineParserTestFixture,
                         ::testing::Values(ExtractCvdArgsTestParams{
                             .input_args = {"cvd"},
                             .expected_args = {"cvd"},
                             .expected_options = selector::SelectorOptions{
                                 .group_name = std::nullopt,
                                 .instance_names = std::nullopt}}));

INSTANTIATE_TEST_SUITE_P(
    SelectNoSubCmd, FrontlineParserTestFixture,
    ::testing::Values(ExtractCvdArgsTestParams{
        .input_args = {"cvd", "--group_name=group", "--instance_name=1"},
        .expected_args = {"cvd"},
        .expected_options = selector::SelectorOptions{
            .group_name = "group",
            .instance_names = std::vector<std::string>{"1"}}}));

INSTANTIATE_TEST_SUITE_P(SelectNoSelectors, FrontlineParserTestFixture,
                         ::testing::Values(ExtractCvdArgsTestParams{
                             .input_args = {"cvd", "subcmd", "arg1", "arg2"},
                             .expected_args = {"cvd", "subcmd", "arg1", "arg2"},
                             .expected_options = selector::SelectorOptions{
                                 .group_name = std::nullopt,
                                 .instance_names = std::nullopt}}));

INSTANTIATE_TEST_SUITE_P(
    SelectGroup, FrontlineParserTestFixture,
    ::testing::Values(ExtractCvdArgsTestParams{
        .input_args = {"cvd", "--group_name=group", "subcmd", "arg1", "arg2"},
        .expected_args = {"cvd", "subcmd", "arg1", "arg2"},
        .expected_options = selector::SelectorOptions{
            .group_name = "group", .instance_names = std::nullopt}}));

INSTANTIATE_TEST_SUITE_P(
    SelectOneInstance, FrontlineParserTestFixture,
    ::testing::Values(ExtractCvdArgsTestParams{
        .input_args = {"cvd", "--instance_name=1", "subcmd", "arg1", "arg2"},
        .expected_args = {"cvd", "subcmd", "arg1", "arg2"},
        .expected_options = selector::SelectorOptions{
            .group_name = std::nullopt,
            .instance_names = std::vector<std::string>{"1"}}}));

INSTANTIATE_TEST_SUITE_P(SelectGroupAndInstances, FrontlineParserTestFixture,
                         ::testing::Values(ExtractCvdArgsTestParams{
                             .input_args = {"cvd", "--instance_name=a,b,c",
                                            "-group_name=group", "subcmd",
                                            "arg1", "arg2"},
                             .expected_args = {"cvd", "subcmd", "arg1", "arg2"},
                             .expected_options = selector::SelectorOptions{
                                 .group_name = "group",
                                 .instance_names = std::vector<std::string>{
                                     "a",
                                     "b",
                                     "c",
                                 }}}));

INSTANTIATE_TEST_SUITE_P(SelectWithEmptyInstanceNames,
                         FrontlineParserTestFixture,
                         ::testing::Values(ExtractCvdArgsTestParams{
                             .input_args = {"cvd", "--instance_name=a,,",
                                            "-group_name=group", "subcmd",
                                            "arg1", "arg2"},
                             .expected_args = {"cvd", "subcmd", "arg1", "arg2"},
                             .expected_options = selector::SelectorOptions{
                                 .group_name = "group",
                                 .instance_names = std::vector<std::string>{
                                     "a",
                                     "",
                                     "",
                                 }}}));

}  // namespace cuttlefish
